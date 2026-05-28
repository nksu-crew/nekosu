package boot

const (
	BootMagic         = "ANDROID!"
	BootMagicSize     = 8
	BootNameSize      = 16
	BootArgsSize      = 512
	BootExtraArgsSize = 1024

	VendorBootMagic     = "VNDRBOOT"
	VendorBootMagicSize = 8
	VendorBootArgsSize  = 2048
	VendorBootNameSize  = 16

	VendorRamdiskTypeNone              = 0
	VendorRamdiskTypePlatform          = 1
	VendorRamdiskTypeRecovery          = 2
	VendorRamdiskTypeDLKM              = 3
	VendorRamdiskNameSize              = 32
	VendorRamdiskTableEntryBoardIDSize = 16
)

// boot_img_hdr_v0
type BootImgHdrV0 struct {
	Magic         [BootMagicSize]byte
	KernelSize    uint32
	KernelAddr    uint32
	RamdiskSize   uint32
	RamdiskAddr   uint32
	SecondSize    uint32
	SecondAddr    uint32
	TagsAddr      uint32
	PageSize      uint32
	HeaderVersion uint32
	OsVersion     uint32
	Name          [BootNameSize]byte
	Cmdline       [BootArgsSize]byte
	Id            [8]uint32
	ExtraCmdline  [BootExtraArgsSize]byte
}

// boot_img_hdr_v1
type BootImgHdrV1 struct {
	Base               BootImgHdrV0
	RecoveryDtboSize   uint32
	RecoveryDtboOffset [8]byte
	HeaderSize         uint32
}

// boot_img_hdr_v2
type BootImgHdrV2 struct {
	Base    BootImgHdrV1
	DtbSize uint32
	DtbAddr [8]byte
}

// boot_img_hdr_v3
type BootImgHdrV3 struct {
	Magic         [BootMagicSize]byte
	KernelSize    uint32
	RamdiskSize   uint32
	OsVersion     uint32
	HeaderSize    uint32
	Reserved      [4]uint32
	HeaderVersion uint32
	Cmdline       [BootArgsSize + BootExtraArgsSize]byte
}

// boot_img_hdr_v4
type BootImgHdrV4 struct {
	Base          BootImgHdrV3
	SignatureSize uint32
}

// vendor_boot_img_hdr_v3
type VendorBootImgHdrV3 struct {
	Magic             [VendorBootMagicSize]byte
	HeaderVersion     uint32
	PageSize          uint32
	KernelAddr        uint32
	RamdiskAddr       uint32
	VendorRamdiskSize uint32
	Cmdline           [VendorBootArgsSize]byte
	TagsAddr          uint32
	Name              [VendorBootNameSize]byte
	HeaderSize        uint32
	DtbSize           uint32
	DtbAddr           [8]byte
}

// vendor_boot_img_hdr_v4
type VendorBootImgHdrV4 struct {
	Base                        VendorBootImgHdrV3
	VendorRamdiskTableSize      uint32
	VendorRamdiskTableEntryNum  uint32
	VendorRamdiskTableEntrySize uint32
	BootconfigSize              uint32
}

// vendor_ramdisk_table_entry_v4
type VendorRamdiskTableEntryV4 struct {
	RamdiskSize   uint32
	RamdiskOffset uint32
	RamdiskType   uint32
	RamdiskName   [VendorRamdiskNameSize]byte
	BoardId       [VendorRamdiskTableEntryBoardIDSize]uint32
}
