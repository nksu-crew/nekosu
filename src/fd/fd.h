/* anonfd.c */
int  fmac_anonfd_init(void);
void fmac_anonfd_exit(void);
int  fmac_anonfd_get(void);
void *fmac_shm_get(void);
size_t shm_size(void);

/* eventfd.c */
int  nksu_bind_eventfd(int fd);
void fmac_notify_user(void);
void fmac_eventfd_exit(void);

/* shm_hash.c */
bool check_mmap_write(void);