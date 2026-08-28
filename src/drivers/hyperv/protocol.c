#include <inferenceos/drivers/hyperv/protocol.h>

/* GUIDs are stored in the same byte order in which VMBus places them on the wire. */
const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_SCSI = {
    {0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
     0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f}
};

const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_KEYBOARD = {
    {0x6d, 0xad, 0x12, 0xf9, 0x17, 0x2b, 0xea, 0x48,
     0xbd, 0x65, 0xf9, 0x27, 0xa6, 0x1c, 0x76, 0x84}
};

const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_MOUSE = {
    {0x9e, 0xb6, 0xa8, 0xcf, 0x4a, 0x5b, 0xc0, 0x4c,
     0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a}
};

bool hyperv_guid_equal(const struct ios_hv_guid *left, const struct ios_hv_guid *right)
{
    ios_u8 difference = 0;

    if (left == NULL || right == NULL) {
        return false;
    }
    for (ios_size index = 0; index < sizeof(left->bytes); ++index) {
        difference |= (ios_u8)(left->bytes[index] ^ right->bytes[index]);
    }
    return difference == 0;
}
