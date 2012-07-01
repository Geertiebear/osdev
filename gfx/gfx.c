// ------------------------------------------------------------------------------------------------
// gfx/gfx.c
// ------------------------------------------------------------------------------------------------

#include "gfx/gfx.h"
#include "gfx/reg.h"
#include "console/console.h"
#include "cpu/io.h"
#include "input/input.h"
#include "mem/vm.h"
#include "net/rlog.h"
#include "pci/registry.h"
#include "stdlib/string.h"
#include "time/pit.h"

#define DEVICE_HD3000 0x0162

// ------------------------------------------------------------------------------------------------
typedef struct GfxDevice
{
    uint    pciId;

    void   *pApertureBar;
    void   *pMMIOBar;
    u32    *pGTTAddr;
    u16     ioBarAddr;
} GfxDevice;

static GfxDevice s_gfxDevice;

// ------------------------------------------------------------------------------------------------
static u32 gfx_read(uint reg)
{
    return *(u32*)((u8*)s_gfxDevice.pMMIOBar + reg);
}

// ------------------------------------------------------------------------------------------------
static void gfx_write(uint reg, u32 value)
{
    *(u32*)((u8*)s_gfxDevice.pMMIOBar + reg) = value;
}

// ------------------------------------------------------------------------------------------------
static void gfx_print_port_state()
{
    rlog_print("HDMI_CTL_B: 0x%08X\n", gfx_read(HDMI_CTL_B));
    rlog_print("HDMI_CTL_C: 0x%08X\n", gfx_read(HDMI_CTL_C));
    rlog_print("HDMI_CTL_D: 0x%08X\n", gfx_read(HDMI_CTL_D));
}

// ------------------------------------------------------------------------------------------------
static void gfx_print_pipe_state()
{
    for (uint i = 0; i < 3; ++i)
    {
        uint pipe = 0x1000 * i;
        rlog_print("PIPE %d\n", i);

        // Output Timing
        rlog_print("  CONF: %08x\n", gfx_read(PIPE_CONF_A + pipe));

        u32 htotal = gfx_read(PIPE_HTOTAL_A + pipe);
        uint hactive = (htotal & 0xffff) + 1;
        uint htotal_size = ((htotal >> 16) & 0xffff) + 1;
        rlog_print("  HTOTAL: %08x %d,%d\n", htotal, hactive, htotal_size);

        u32 hblank = gfx_read(PIPE_HBLANK_A + pipe);
        uint hblank_start = (hblank & 0xffff) + 1;
        uint hblank_end = ((hblank >> 16) & 0xffff) + 1;
        rlog_print("  HBLANK: %08x %d,%d\n", hblank, hblank_start, hblank_end);

        u32 hsync = gfx_read(PIPE_HSYNC_A + pipe);
        uint hsync_start = (hsync & 0xffff) + 1;
        uint hsync_end = ((hsync >> 16) & 0xffff) + 1;
        rlog_print("  HSYNC: %08x %d,%d\n", hsync, hsync_start, hsync_end);

        u32 vtotal = gfx_read(PIPE_VTOTAL_A + pipe);
        uint vactive = (vtotal & 0xffff) + 1;
        uint vtotal_size = ((vtotal >> 16) & 0xffff) + 1;
        rlog_print("  VTOTAL: %08x %d,%d\n", vtotal, vactive, vtotal_size);

        u32 vblank = gfx_read(PIPE_VBLANK_A + pipe);
        uint vblank_start = (vblank & 0xffff) + 1;
        uint vblank_end = ((vblank >> 16) & 0xffff) + 1;
        rlog_print("  VBLANK: %08x %d,%d\n", vblank, vblank_start, vblank_end);

        u32 vsync = gfx_read(PIPE_VSYNC_A + pipe);
        uint vsync_start = (vsync & 0xffff) + 1;
        uint vsync_end = ((vsync >> 16) & 0xffff) + 1;
        rlog_print("  VSYNC: %08x %d,%d\n", vsync, vsync_start, vsync_end);

        u32 srcsz = gfx_read(PIPE_SRCSZ_A + pipe);
        uint width = ((srcsz >> 16) & 0xfff) + 1;
        uint height = (srcsz & 0xffff) + 1;
        rlog_print("  SRCSZ: %08x %dx%d\n", srcsz, width, height);

        // Cursor Plane
        rlog_print("  CUR_CTL: %08x\n", gfx_read(CUR_CTL_A + pipe));
        rlog_print("  CUR_BASE: %08x\n", gfx_read(CUR_BASE_A + pipe));
        rlog_print("  CUR_POS: %08x\n", gfx_read(CUR_POS_A + pipe));
    }
}

// ------------------------------------------------------------------------------------------------
void gfx_init(uint id, PCI_DeviceInfo* info)
{
    if (!(((info->class_code << 8) | info->subclass) == PCI_DISPLAY_VGA &&
        info->prog_intf == 0))
    {
        return;
    }

    if ((info->vendor_id != VENDOR_INTEL) ||
        (info->device_id != DEVICE_HD3000))
    {
        console_print("Graphics Controller not recognised!\n");
        return;
    }


    memset(&s_gfxDevice, 0, sizeof(s_gfxDevice));
    s_gfxDevice.pciId = id;
}

// ------------------------------------------------------------------------------------------------
void gfx_start()
{
    // Read PCI registers
    PCI_Bar bar;

    rlog_print("...Probing PCIe Config:\n");
    rlog_print("    PCI id:       0x%X\n",             s_gfxDevice.pciId);

    // GTTMMADDR
    pci_get_bar(&bar, s_gfxDevice.pciId, 0);
    s_gfxDevice.pMMIOBar = bar.u.address;
    s_gfxDevice.pGTTAddr = (u32*)((u8*)bar.u.address + (2 * 1024 * 1024));
    rlog_print("    GTTMMADR:     0x%llX (%llu MB)\n", bar.u.address, bar.size / (1024 * 1024));

    // GMADR
    pci_get_bar(&bar, s_gfxDevice.pciId, 2);
    s_gfxDevice.pApertureBar = bar.u.address;
    rlog_print("    GMADR:        0x%llX (%llu MB)\n", bar.u.address, bar.size / (1024 * 1024));

    // IOBASE
    pci_get_bar(&bar, s_gfxDevice.pciId, 4);
    s_gfxDevice.ioBarAddr = bar.u.port;
    rlog_print("    IOBASE:       0x%X (%u bytes)\n", bar.u.port, bar.size);

    // Log initial port state
    gfx_print_port_state();

    // Disable the VGA Plane
    out8(SR_INDEX, SEQ_CLOCKING);
    out8(SR_DATA, in8(SR_DATA) | SCREEN_OFF);
    pit_wait(100);
    gfx_write(VGA_CONTROL, VGA_DISABLE);

    rlog_print("VGA Plane disabled\n");

    // Initialize Graphics Memory
    u8* gfx_mem = vm_alloc(512 * 1024 * 1024);      // TODO: how to know size of GTT?
    u8* cursor_mem = gfx_mem;
    //memset(gfx_mem, 0xff, 512 * 1024 * 1024);
    memset(cursor_mem, 0xff, 64 * 64 * sizeof(u32));    // TODO: this isn't the area the GPU is reading?

    // Setup Virtual Memory
    u32* gtt_addr = (u32*)((u8*)s_gfxDevice.pMMIOBar + (2 * 1024 * 1024));
    u8* phys_page = gfx_mem;
    for (uint i = 0; i < 512 * 1024; ++i)
    {
        uintptr_t addr = (uintptr_t)phys_page;

        // Mark for LLC Cache use and Valid
        gtt_addr[i] = addr | ((addr >> 28) & 0xff0) | (2 << 1) | (1 << 0);

        phys_page += 4096;
    }

    // Setup Cursor Plane
    u32 cursor_gfx_mem = (u32)(cursor_mem - gfx_mem);
    gfx_write(CUR_CTL_A, (1 << 5) | 0x7);   // 64x64 32bpp ARGB
    gfx_write(CUR_BASE_A, cursor_gfx_mem);  // TODO - use 64k alignment with 2 extra PTEs?

    // Pipe State
    gfx_print_pipe_state();

}

// ------------------------------------------------------------------------------------------------
void gfx_poll()
{
    // Update cursor position
    gfx_write(CUR_POS_A, (g_mouse_y << 16) | g_mouse_x);
}
