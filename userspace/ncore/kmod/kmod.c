#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <elf.h>

#include "kmod.h"
#include "hashtable.h"

#define KPTR_PATH "/proc/sys/kernel/kptr_restrict"

static char s_kptr_saved = 0;
static int s_kptr_inited = 0;

static int kptr_set(char code)
{
    int fd = open(KPTR_PATH, O_RDWR);
    if (fd < 0)
        return -errno;

    char buf = 0;
    if (read(fd, &buf, 1) == 1) {
        s_kptr_saved = buf;
        s_kptr_inited = 1;
    }

    lseek(fd, 0, SEEK_SET);
    write(fd, &code, 1);
    close(fd);
    return 0;
}

static void kptr_reset(void)
{
    if (!s_kptr_inited)
        return;
    int fd = open(KPTR_PATH, O_WRONLY);
    if (fd < 0)
        return;
    write(fd, &s_kptr_saved, 1);
    close(fd);
}

/* Strips "$..." and ".llvm...." suffixes in-place, returns original ptr */
static const char *normalize_symbol(const char *sym, char *buf, size_t bufsz)
{
    const char *cut = NULL;
    const char *p;

    if ((p = strchr(sym, '$')))
        if (!cut || p < cut)
            cut = p;
    if ((p = strstr(sym, ".llvm.")))
        if (!cut || p < cut)
            cut = p;

    if (!cut)
        return sym;

    size_t len = (size_t)(cut - sym);
    if (len >= bufsz)
        len = bufsz - 1;
    memcpy(buf, sym, len);
    buf[len] = '\0';
    return buf;
}

static uint64_t hex_to_u64(const char *s)
{
    uint64_t v = 0;
    for (; *s; s++) {
        v <<= 4;
        if (*s >= '0' && *s <= '9')
            v |= (uint64_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f')
            v |= (uint64_t)(*s - 'a') + 10;
        else if (*s >= 'A' && *s <= 'F')
            v |= (uint64_t)(*s - 'A') + 10;
        else
            return 0;
    }
    return v;
}

static int parse_kallsyms(HashTable *ht)
{
    if (kptr_set('1') < 0) {
        fprintf(stderr, "kptr_set failed: %s\n", strerror(errno));
        return -errno;
    }

    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f) {
        kptr_reset();
        return -errno;
    }

    char line[256];
    char nbuf[256];

    while (fgets(line, sizeof(line), f)) {
        /* Format: <addr> <type> <name>[\t<module>] */
        char *tok = strtok(line, " \t\n");
        if (!tok)
            continue;
        uint64_t addr = hex_to_u64(tok);

        tok = strtok(NULL, " \t\n"); /* type — skip */
        if (!tok)
            continue;

        tok = strtok(NULL, " \t\n"); /* name */
        if (!tok)
            continue;

        const char *name = normalize_symbol(tok, nbuf, sizeof(nbuf));

        if (ht_put(ht, name, addr) < 0) {
            fclose(f);
            kptr_reset();
            return -ENOMEM;
        }
    }

    fclose(f);
    kptr_reset();
    return 0;
}

static int load_module(const void *image, size_t size)
{
    const char params[] = "";
    long ret = syscall(SYS_init_module, image, (unsigned long)size, params);
    if (ret < 0) {
        fprintf(stderr, "init_module: %s\n", strerror(errno));
        return -errno;
    }
    return 0;
}

static int patch_and_load(const char *path, const HashTable *ksyms)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -errno;
    }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    rewind(fp);

    uint8_t *image = malloc((size_t)fsz);
    if (!image) {
        fclose(fp);
        return -ENOMEM;
    }
    if ((long)fread(image, 1, (size_t)fsz, fp) != fsz) {
        fclose(fp);
        free(image);
        return -EIO;
    }
    fclose(fp);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)image;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "not a valid ELF file\n");
        free(image);
        return -EINVAL;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "only ELF64 supported\n");
        free(image);
        return -EINVAL;
    }

    Elf64_Shdr *shdr = (Elf64_Shdr *)(image + ehdr->e_shoff);
    Elf64_Shdr *sym_shdr = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            sym_shdr = &shdr[i];
            break;
        }
    }

    if (!sym_shdr) {
        fprintf(stderr, "no SYMTAB section\n");
        free(image);
        return -EINVAL;
    }

    Elf64_Shdr *str_shdr = &shdr[sym_shdr->sh_link];
    const char *strtab = (const char *)(image + str_shdr->sh_offset);

    Elf64_Sym *symtab = (Elf64_Sym *)(image + sym_shdr->sh_offset);
    size_t sym_count = sym_shdr->sh_size / sym_shdr->sh_entsize;
    char nbuf[256];
    int rc = 0;

    for (size_t i = 0; i < sym_count; i++) {
        Elf64_Sym *sym = &symtab[i];

        if (sym->st_shndx != SHN_UNDEF || sym->st_name == 0)
            continue;

        if (sym->st_name >= str_shdr->sh_size)
            continue;

        const char *raw_name = strtab + sym->st_name;
        const char *name = normalize_symbol(raw_name, nbuf, sizeof(nbuf));

        uint64_t addr = ht_get(ksyms, name, NULL);
        if (addr == 0) {
            fprintf(stderr, "missing symbol: %s\n", name);
            rc = -ENOENT;
            goto out;
        }

        printf("Patching symbol %s -> 0x%lx\n", name, (unsigned long)addr);

        sym->st_shndx = SHN_ABS;
        sym->st_value = addr;
    }

    rc = load_module(image, (size_t)fsz);

out:
    free(image);
    return rc;
}

int kmod_load(const char *path)
{
    HashTable *ksyms = ht_create(262144);
    if (!ksyms) {
        fprintf(stderr, "failed to create hashtable\n");
        return -ENOMEM;
    }

    int rc = parse_kallsyms(ksyms);
    if (rc < 0) {
        fprintf(stderr, "parse_kallsyms failed: %s\n", strerror(-rc));
        ht_free(ksyms);
        return rc;
    }

    rc = patch_and_load(path, ksyms);

    ht_free(ksyms);
    return rc;
}
