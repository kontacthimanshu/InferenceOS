#include "loader.h"

#include <inferenceos/runtime.h>

enum { ELF_LOAD = 1, ELF_EXECUTABLE = 2, ELF_X86_64 = 62, ELF_READ = 4, ELF_WRITE = 2, ELF_EXECUTE = 1 };
struct elf64_header {
    ios_u8 identification[16]; ios_u16 type; ios_u16 machine; ios_u32 version;
    ios_u64 entry; ios_u64 program_offset; ios_u64 section_offset; ios_u32 flags;
    ios_u16 header_size; ios_u16 program_size; ios_u16 program_count;
    ios_u16 section_size; ios_u16 section_count; ios_u16 section_names;
};
struct elf64_program {
    ios_u32 type; ios_u32 flags; ios_u64 offset; ios_u64 virtual_address;
    ios_u64 physical_address; ios_u64 file_size; ios_u64 memory_size; ios_u64 alignment;
};
IOS_STATIC_ASSERT(sizeof(struct elf64_header) == 64, "ELF64 header size");
IOS_STATIC_ASSERT(sizeof(struct elf64_program) == 56, "ELF64 program size");

static bool add_overflows(ios_u64 left, ios_u64 right) { return right > UINT64_MAX - left; }
static const struct elf64_program *program_at(
    const ios_u8 *image, const struct elf64_header *header, ios_u16 index
) {
    return (const void *)(image + header->program_offset + (ios_u64)index * header->program_size);
}

ios_status ios_uefi_elf64_load_kernel(
    const void *image_pointer, ios_size image_size, ios_uefi_allocate_image allocate_image,
    void *context, ios_uptr *entry_point, ios_uptr *lowest_address, ios_u64 *loaded_size
) {
    const ios_u8 *image = image_pointer;
    const struct elf64_header *header = image_pointer;
    ios_uptr lowest = UINT64_MAX;
    ios_u64 highest = 0;
    bool entry_is_executable = false;
    if (image == NULL || allocate_image == NULL || entry_point == NULL || lowest_address == NULL
        || loaded_size == NULL || image_size < sizeof(*header)
        || *header->identification != UINT8_C(0x7f) || header->identification[1] != 'E'
        || header->identification[2] != 'L' || header->identification[3] != 'F'
        || header->identification[4] != 2 || header->identification[5] != 1
        || header->identification[6] != 1 || header->type != ELF_EXECUTABLE
        || header->machine != ELF_X86_64 || header->version != 1
        || header->header_size != sizeof(*header) || header->program_size != sizeof(struct elf64_program)
        || header->program_count == 0 || header->program_offset > image_size
        || (ios_u64)header->program_count * header->program_size > image_size - header->program_offset) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_u16 index = 0; index < header->program_count; ++index) {
        const struct elf64_program *segment = program_at(image, header, index);
        if (segment->type != ELF_LOAD || segment->memory_size == 0) { continue; }
        if ((segment->flags & ELF_READ) == 0 || (segment->flags & ~(ios_u32)7) != 0
            || (segment->flags & (ELF_WRITE | ELF_EXECUTE)) == (ELF_WRITE | ELF_EXECUTE)
            || segment->file_size > segment->memory_size || segment->offset > image_size
            || segment->file_size > image_size - segment->offset || segment->alignment != 4096
            || (segment->physical_address & 4095U) != (segment->offset & 4095U)
            || add_overflows(segment->physical_address, segment->memory_size)
            || segment->physical_address + segment->memory_size > UINT64_C(0x40000000)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        for (ios_u16 other_index = 0; other_index < index; ++other_index) {
            const struct elf64_program *other = program_at(image, header, other_index);
            if (other->type == ELF_LOAD && other->memory_size != 0
                && segment->physical_address < other->physical_address + other->memory_size
                && other->physical_address < segment->physical_address + segment->memory_size) {
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
        }
        if ((segment->flags & ELF_EXECUTE) != 0 && header->entry >= segment->virtual_address
            && header->entry - segment->virtual_address < segment->memory_size) {
            entry_is_executable = true;
        }
        if (segment->physical_address < lowest) { lowest = (ios_uptr)segment->physical_address; }
        if (segment->physical_address + segment->memory_size > highest) {
            highest = segment->physical_address + segment->memory_size;
        }
    }
    if (!entry_is_executable || lowest == UINT64_MAX) { return IOS_ERROR(IOS_E_BAD_ADDRESS); }
    for (ios_u16 index = 0; index < header->program_count; ++index) {
        const struct elf64_program *segment = program_at(image, header, index);
        ios_uptr allocation;
        if (segment->type != ELF_LOAD || segment->memory_size == 0) { continue; }
        const ios_uptr page_base = (ios_uptr)segment->physical_address & ~(ios_uptr)4095;
        const ios_size page_bytes = (ios_size)(((segment->physical_address & 4095U)
            + segment->memory_size + 4095U) & ~(ios_u64)4095);
        ios_status status = allocate_image(context, page_bytes, page_base, true, &allocation);
        if (IOS_FAILED(status) || allocation != page_base) {
            return IOS_FAILED(status) ? status : IOS_ERROR(IOS_E_BAD_ADDRESS);
        }
        memset((void *)allocation, 0, page_bytes);
        memcpy((ios_u8 *)allocation + (segment->physical_address & 4095U),
            image + segment->offset, (ios_size)segment->file_size);
    }
    *entry_point = (ios_uptr)header->entry;
    *lowest_address = lowest;
    *loaded_size = highest - lowest;
    return IOS_OK;
}
