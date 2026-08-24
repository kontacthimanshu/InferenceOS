#include <inferenceos/process.h>

#include <inferenceos/runtime.h>

enum {
    ELF_CLASS_64 = 2,
    ELF_DATA_LITTLE_ENDIAN = 1,
    ELF_VERSION_CURRENT = 1,
    ELF_TYPE_EXECUTABLE = 2,
    ELF_MACHINE_X86_64 = 62,
    ELF_PROGRAM_LOAD = 1,
    ELF_SEGMENT_EXECUTE = 1,
    ELF_SEGMENT_WRITE = 2,
    ELF_SEGMENT_READ = 4
};

struct elf64_header {
    ios_u8 identification[16];
    ios_u16 type;
    ios_u16 machine;
    ios_u32 version;
    ios_u64 entry;
    ios_u64 program_header_offset;
    ios_u64 section_header_offset;
    ios_u32 flags;
    ios_u16 header_size;
    ios_u16 program_header_size;
    ios_u16 program_header_count;
    ios_u16 section_header_size;
    ios_u16 section_header_count;
    ios_u16 section_name_index;
};

struct elf64_program_header {
    ios_u32 type;
    ios_u32 flags;
    ios_u64 offset;
    ios_u64 virtual_address;
    ios_u64 physical_address;
    ios_u64 file_size;
    ios_u64 memory_size;
    ios_u64 alignment;
};

IOS_STATIC_ASSERT(sizeof(struct elf64_header) == 64, "ELF64 header size");
IOS_STATIC_ASSERT(sizeof(struct elf64_program_header) == 56, "ELF64 program header size");

static ios_status validate_header(
    const struct elf64_header *header,
    ios_size image_size
)
{
    ios_u64 table_size;

    if (image_size < sizeof(*header) || *header->identification != UINT8_C(0x7f)
        || header->identification[1] != 'E' || header->identification[2] != 'L'
        || header->identification[3] != 'F' || header->identification[4] != ELF_CLASS_64
        || header->identification[5] != ELF_DATA_LITTLE_ENDIAN
        || header->identification[6] != ELF_VERSION_CURRENT
        || header->type != ELF_TYPE_EXECUTABLE || header->machine != ELF_MACHINE_X86_64
        || header->version != ELF_VERSION_CURRENT || header->header_size != sizeof(*header)
        || header->program_header_size != sizeof(struct elf64_program_header)
        || header->program_header_count == 0
        || (header->program_header_offset & (sizeof(ios_u64) - 1U)) != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    table_size = (ios_u64)header->program_header_count * header->program_header_size;
    if (header->program_header_offset > image_size
        || table_size > image_size - header->program_header_offset) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    return IOS_OK;
}

static const struct elf64_program_header *program_header_at(
    const ios_u8 *image,
    const struct elf64_header *header,
    ios_u16 index
)
{
    return (const struct elf64_program_header *)(const void *)(
        image + header->program_header_offset + (ios_u64)index * header->program_header_size
    );
}

static ios_status validate_segment(
    const struct elf64_program_header *segment,
    ios_size image_size
)
{
    if (segment->memory_size == 0) {
        return IOS_OK;
    }
    if ((segment->flags & ~(ELF_SEGMENT_READ | ELF_SEGMENT_WRITE | ELF_SEGMENT_EXECUTE)) != 0
        || (segment->flags & ELF_SEGMENT_READ) == 0
        || (segment->flags & (ELF_SEGMENT_WRITE | ELF_SEGMENT_EXECUTE))
            == (ELF_SEGMENT_WRITE | ELF_SEGMENT_EXECUTE)
        || segment->file_size > segment->memory_size || segment->offset > image_size
        || segment->file_size > image_size - segment->offset
        || segment->alignment != IOS_PAGE_SIZE
        || (segment->virtual_address & (IOS_PAGE_SIZE - 1U))
            != (segment->offset & (IOS_PAGE_SIZE - 1U))
        || !virtual_user_range_is_valid(segment->virtual_address, segment->memory_size)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

static bool segments_overlap(
    const struct elf64_program_header *left,
    const struct elf64_program_header *right
)
{
    const ios_u64 left_base = left->virtual_address & ~(ios_u64)(IOS_PAGE_SIZE - 1U);
    const ios_u64 right_base = right->virtual_address & ~(ios_u64)(IOS_PAGE_SIZE - 1U);
    const ios_u64 left_end = (left->virtual_address + left->memory_size
        + IOS_PAGE_SIZE - 1U) & ~(ios_u64)(IOS_PAGE_SIZE - 1U);
    const ios_u64 right_end = (right->virtual_address + right->memory_size
        + IOS_PAGE_SIZE - 1U) & ~(ios_u64)(IOS_PAGE_SIZE - 1U);

    return left->memory_size != 0 && right->memory_size != 0
        && left_base < right_end && right_base < left_end;
}

static ios_status map_segment(
    struct ios_process *process,
    const ios_u8 *image,
    const struct elf64_program_header *segment
)
{
    const ios_uptr page_offset = segment->virtual_address & (IOS_PAGE_SIZE - 1U);
    const ios_uptr mapping_base = segment->virtual_address - page_offset;
    const ios_u64 mapped_bytes = page_offset + segment->memory_size;
    const ios_u64 page_count = (mapped_bytes + IOS_PAGE_SIZE - 1U) / IOS_PAGE_SIZE;
    ios_uptr physical_address;
    ios_u32 mapping_flags = IOS_VM_USER | IOS_VM_OWNED;
    ios_status status;

    status = physical_allocate_pages(page_count, 1, &physical_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    memset((void *)physical_address, 0, (ios_size)(page_count * IOS_PAGE_SIZE));
    memcpy(
        (ios_u8 *)physical_address + page_offset,
        image + segment->offset,
        (ios_size)segment->file_size
    );
    if ((segment->flags & ELF_SEGMENT_WRITE) != 0) {
        mapping_flags |= IOS_VM_WRITE;
    }
    if ((segment->flags & ELF_SEGMENT_EXECUTE) != 0) {
        mapping_flags |= IOS_VM_EXECUTE;
    }
    status = virtual_map_pages(
        &process->address_space, mapping_base, physical_address, page_count, mapping_flags
    );
    if (IOS_FAILED(status)) {
        (void)physical_free_pages(physical_address, page_count);
    }
    return status;
}

ios_status process_load_static_elf64(
    struct ios_process *process,
    const void *image_pointer,
    ios_size image_size
)
{
    const ios_u8 *image = image_pointer;
    const struct elf64_header *header;
    bool entry_is_executable = false;
    ios_status status;

    if (process == NULL || image == NULL || process->address_space.root_address == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    header = (const struct elf64_header *)(const void *)image;
    status = validate_header(header, image_size);
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_u16 index = 0; index < header->program_header_count; ++index) {
        const struct elf64_program_header *segment = program_header_at(image, header, index);
        if (segment->type != ELF_PROGRAM_LOAD) {
            continue;
        }
        status = validate_segment(segment, image_size);
        if (IOS_FAILED(status)) {
            return status;
        }
        if ((segment->flags & ELF_SEGMENT_EXECUTE) != 0
            && header->entry >= segment->virtual_address
            && header->entry - segment->virtual_address < segment->memory_size) {
            entry_is_executable = true;
        }
        for (ios_u16 other_index = 0; other_index < index; ++other_index) {
            const struct elf64_program_header *other = program_header_at(
                image, header, other_index
            );
            if (other->type == ELF_PROGRAM_LOAD && segments_overlap(segment, other)) {
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
        }
    }
    if (!entry_is_executable) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    for (ios_u16 index = 0; index < header->program_header_count; ++index) {
        const struct elf64_program_header *segment = program_header_at(image, header, index);
        if (segment->type == ELF_PROGRAM_LOAD && segment->memory_size != 0) {
            status = map_segment(process, image, segment);
            if (IOS_FAILED(status)) {
                return status;
            }
        }
    }
    process->entry_point = (ios_uptr)header->entry;
    return IOS_OK;
}
