# NekoSU
## Kernel-Level Root Access Framework

> A sophisticated kernel-level root management framework for modern Android systems, built with precision and security in focus.

---

## 🎯 Core Capabilities

### **Kernel Base Root Access**
Direct, kernel-enforced root privilege escalation with fine-grained control over process execution and system permissions.

---

## 📋 Technical Highlights

| Feature | Description |
|---------|-------------|
| **Kernel Integration** | Direct GKI kernel module integration (android13-5.15 through android16-6.12) |
| **Architecture** | Supports ARM64 with clean separation: kernel module (C), native library (Golang/JNI), Android app (Kotlin/Jetpack Compose) |
| **Policy Injection** | Runtime SELinux policy modification and dynamic capability management |
| **Process Control** | Fine-grained per-UID privilege profiles with RCU-protected caching |

---

## 🚀 Getting Started

### Prerequisites
- Android 13+ (GKI kernel)
- ARM64 architecture
- Appropriate kernel development environment

### Installation
See [INSTALLATION.md](./docs/INSTALLATION.md) for detailed setup instructions.

---

## 📚 Documentation
// TODO:
---

## 🤝 Contributing

Contributions are welcome! Please:
1. Review our [CONTRIBUTING.md](./CONTRIBUTING.md) guidelines
2. Submit pull requests with clear commit messages
3. Open issues to discuss major changes or report bugs

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0**.

For full details, see the [LICENSE](./LICENSE) file.

> **Note**: NekoSU is provided as-is. Users assume full responsibility for system integrity and security implications of its use.

---

## 📞 Community

- **Issues**: Report bugs and feature requests on [GitHub Issues](https://github.com/FMAC-Team/nekosu/issues)
- **Discussions**: Join technical discussions on [GitHub Discussions](https://github.com/FMAC-Team/nekosu/discussions)

---

<div align="center">

**Made with precision by FMAC Team**

[GitHub](https://github.com/FMAC-Team/nekosu) • [Issues](https://github.com/FMAC-Team/nekosu/issues) • [License](./LICENSE)

</div>
