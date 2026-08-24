#include <inferenceos/arch/io.h>
#include <inferenceos/arch/platform.h>

enum {
    PIC_MASTER_COMMAND = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_COMMAND = 0xa0,
    PIC_SLAVE_DATA = 0xa1,
    PIC_INITIALIZE = 0x11,
    PIC_8086_MODE = 0x01
};

static void io_wait(void)
{
    x86_64_port_write8(0x80, 0);
}

void x86_64_pic_mask_and_remap(void)
{
    x86_64_port_write8(PIC_MASTER_DATA, UINT8_MAX);
    x86_64_port_write8(PIC_SLAVE_DATA, UINT8_MAX);
    x86_64_port_write8(PIC_MASTER_COMMAND, PIC_INITIALIZE);
    io_wait();
    x86_64_port_write8(PIC_SLAVE_COMMAND, PIC_INITIALIZE);
    io_wait();
    x86_64_port_write8(PIC_MASTER_DATA, 0x20);
    io_wait();
    x86_64_port_write8(PIC_SLAVE_DATA, 0x28);
    io_wait();
    x86_64_port_write8(PIC_MASTER_DATA, 4);
    io_wait();
    x86_64_port_write8(PIC_SLAVE_DATA, 2);
    io_wait();
    x86_64_port_write8(PIC_MASTER_DATA, PIC_8086_MODE);
    io_wait();
    x86_64_port_write8(PIC_SLAVE_DATA, PIC_8086_MODE);
    io_wait();
    x86_64_port_write8(PIC_MASTER_DATA, UINT8_MAX);
    x86_64_port_write8(PIC_SLAVE_DATA, UINT8_MAX);
}
