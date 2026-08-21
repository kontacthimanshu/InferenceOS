"""Deterministic InferenceFS-FAT32 v1 mount-image fixtures for US2."""

from __future__ import annotations

import struct
import tempfile
import unittest
import zlib
from collections.abc import Callable
from pathlib import Path


SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 8
TOTAL_SECTORS = 16 * 1024 * 1024 // SECTOR_SIZE
IMAGE_SIZE = TOTAL_SECTORS * SECTOR_SIZE
RESERVED_SECTORS = 2
SECTORS_PER_FAT = 32
DATA_START_LBA = RESERVED_SECTORS + SECTORS_PER_FAT
ROOT_CLUSTER = 2
EOC = 0x0FFFFFFF

CLEAN_WRITABLE = "clean-writable"
DIAGNOSTIC_READ_ONLY = "diagnostic-read-only"
REJECTED = "rejected"


def _set_crc(superblock: bytearray) -> None:
    struct.pack_into("<I", superblock, 0x38, 0)
    struct.pack_into("<I", superblock, 0x38, zlib.crc32(superblock[:0x40]))


def _superblock() -> bytearray:
    block = bytearray(SECTOR_SIZE)
    block[0:8] = b"INFFAT32"
    struct.pack_into("<HHHBBHHHH", block, 0x08,
                     1, 64, SECTOR_SIZE, SECTORS_PER_CLUSTER, 1,
                     RESERVED_SECTORS, 32, 32, 0)
    struct.pack_into("<IIII", block, 0x18,
                     TOTAL_SECTORS, SECTORS_PER_FAT, ROOT_CLUSTER, 0x1234ABCD)
    block[0x28:0x33] = b"INFERENCE  "
    block[0x33] = 1
    struct.pack_into("<HH", block, 0x34, 1, 1)
    struct.pack_into("<H", block, 0x1FE, 0xAA55)
    _set_crc(block)
    return block


def _base_image() -> bytearray:
    image = bytearray(IMAGE_SIZE)
    block = _superblock()
    image[0:SECTOR_SIZE] = block
    image[SECTOR_SIZE:2 * SECTOR_SIZE] = block
    fat = RESERVED_SECTORS * SECTOR_SIZE
    struct.pack_into("<III", image, fat, 0x0FFFFFF8, EOC, EOC)
    return image


def _rewrite_superblock(image: bytearray, lba: int, mutate: Callable[[bytearray], None]) -> None:
    start = lba * SECTOR_SIZE
    block = bytearray(image[start:start + SECTOR_SIZE])
    mutate(block)
    _set_crc(block)
    image[start:start + SECTOR_SIZE] = block


def _root_offset() -> int:
    return DATA_START_LBA * SECTOR_SIZE


def _directory_record(name: bytes, cluster: int, attributes: int = 0x10) -> bytes:
    if len(name) != 11:
        raise ValueError("short name must occupy exactly 11 bytes")
    record = bytearray(32)
    record[0:11] = name
    record[11] = attributes
    struct.pack_into("<H", record, 20, cluster >> 16)
    struct.pack_into("<H", record, 26, cluster & 0xFFFF)
    return bytes(record)


def _valid(image: bytearray) -> None:
    del image


def _backup_only(image: bytearray) -> None:
    image[0] ^= 0x01


def _differing(image: bytearray) -> None:
    _rewrite_superblock(image, 1, lambda block: struct.pack_into("<I", block, 0x24, 7))


def _corrupt(image: bytearray) -> None:
    image[0x38] ^= 0x80
    image[SECTOR_SIZE + 0x38] ^= 0x80


def _unsupported(image: bytearray) -> None:
    for lba in (0, 1):
        _rewrite_superblock(image, lba, lambda block: struct.pack_into("<H", block, 0x08, 2))


def _nonzero_reserved(image: bytearray) -> None:
    for lba in (0, 1):
        _rewrite_superblock(image, lba, lambda block: block.__setitem__(0x40, 1))


def _unsupported_attribute(image: bytearray) -> None:
    root = _root_offset()
    image[root:root + 32] = _directory_record(b"LABEL      ", 0, 0x08)


def _cross_linked_chain(image: bytearray) -> None:
    fat = RESERVED_SECTORS * SECTOR_SIZE
    struct.pack_into("<I", image, fat + 3 * 4, EOC)
    root = _root_offset()
    image[root:root + 32] = _directory_record(b"FIRST      ", 3)
    image[root + 32:root + 64] = _directory_record(b"SECOND     ", 3)


def _impossible_geometry(image: bytearray) -> None:
    for lba in (0, 1):
        _rewrite_superblock(
            image, lba,
            lambda block: struct.pack_into("<I", block, 0x1C, TOTAL_SECTORS))


FIXTURES: dict[str, tuple[str, Callable[[bytearray], None]]] = {
    "valid": (CLEAN_WRITABLE, _valid),
    "backup-only": (DIAGNOSTIC_READ_ONLY, _backup_only),
    "differing": (REJECTED, _differing),
    "corrupt": (REJECTED, _corrupt),
    "unsupported": (REJECTED, _unsupported),
    "nonzero-reserved-field": (REJECTED, _nonzero_reserved),
    "unsupported-attribute": (REJECTED, _unsupported_attribute),
    "cross-linked-chain": (REJECTED, _cross_linked_chain),
    "impossible-geometry": (REJECTED, _impossible_geometry),
}


def build_fixture(name: str) -> bytes:
    image = _base_image()
    FIXTURES[name][1](image)
    return bytes(image)


def _decode_superblock(block: bytes) -> tuple[str, tuple[int, ...] | None]:
    if len(block) != SECTOR_SIZE or block[0:8] != b"INFFAT32":
        return "invalid", None
    stored_crc = struct.unpack_from("<I", block, 0x38)[0]
    crc_input = bytearray(block[:0x40])
    struct.pack_into("<I", crc_input, 0x38, 0)
    if zlib.crc32(crc_input) != stored_crc:
        return "invalid", None
    version, header_size, sector_size = struct.unpack_from("<HHH", block, 0x08)
    sectors_per_cluster, fat_count = struct.unpack_from("<BB", block, 0x0E)
    reserved, directory_size, companion_size, flags = struct.unpack_from("<HHHH", block, 0x10)
    total, fat_sectors, root = struct.unpack_from("<III", block, 0x18)
    if version != 1:
        return "unsupported", None
    if (header_size, sector_size, sectors_per_cluster, fat_count, reserved,
            directory_size, companion_size, flags, root) != (64, 512, 8, 1, 2, 32, 32, 0, 2):
        return "invalid", None
    if block[0x3C:0x1FE] != bytes(0x1FE - 0x3C):
        return "invalid", None
    if struct.unpack_from("<H", block, 0x1FE)[0] != 0xAA55:
        return "invalid", None
    data_start = reserved + fat_sectors
    if total != TOTAL_SECTORS or fat_sectors == 0 or data_start >= total:
        return "invalid", None
    clusters = (total - data_start) // sectors_per_cluster
    if fat_sectors * sector_size // 4 < clusters + 2:
        return "invalid", None
    return "valid", (total, fat_sectors, root, stored_crc, *block[0x24:0x38])


def classify_fixture(image: bytes) -> str:
    primary_status, primary = _decode_superblock(image[:SECTOR_SIZE])
    backup_status, backup = _decode_superblock(image[SECTOR_SIZE:2 * SECTOR_SIZE])
    if primary_status != "valid":
        return DIAGNOSTIC_READ_ONLY if backup_status == "valid" else REJECTED
    if backup_status != "valid" or primary != backup:
        return REJECTED

    fat = RESERVED_SECTORS * SECTOR_SIZE
    if struct.unpack_from("<III", image, fat) != (0x0FFFFFF8, EOC, EOC):
        return REJECTED
    root = _root_offset()
    owners: set[int] = {ROOT_CLUSTER}
    for offset in range(root, root + SECTORS_PER_CLUSTER * SECTOR_SIZE, 32):
        record = image[offset:offset + 32]
        if record[0] == 0:
            break
        attributes = record[11]
        if attributes not in (0x10, 0x20):
            return REJECTED
        cluster = (struct.unpack_from("<H", record, 20)[0] << 16) | struct.unpack_from("<H", record, 26)[0]
        if cluster in owners:
            return REJECTED
        owners.add(cluster)
    return CLEAN_WRITABLE


class SuperblockMountImageTests(unittest.TestCase):
    def test_fixture_matrix_has_required_mount_outcomes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="inferencefs-superblocks-") as temporary:
            directory = Path(temporary)
            for name, (expected, _) in FIXTURES.items():
                with self.subTest(name=name):
                    image = build_fixture(name)
                    path = directory / f"{name}.img"
                    path.write_bytes(image)
                    self.assertEqual(IMAGE_SIZE, path.stat().st_size)
                    self.assertEqual(expected, classify_fixture(path.read_bytes()))

    def test_backup_only_preserves_an_independently_valid_backup(self) -> None:
        image = build_fixture("backup-only")
        self.assertEqual("invalid", _decode_superblock(image[:SECTOR_SIZE])[0])
        self.assertEqual("valid", _decode_superblock(image[SECTOR_SIZE:2 * SECTOR_SIZE])[0])

    def test_differing_superblocks_are_individually_valid(self) -> None:
        image = build_fixture("differing")
        primary = _decode_superblock(image[:SECTOR_SIZE])
        backup = _decode_superblock(image[SECTOR_SIZE:2 * SECTOR_SIZE])
        self.assertEqual("valid", primary[0])
        self.assertEqual("valid", backup[0])
        self.assertNotEqual(primary[1], backup[1])

    def test_cross_link_fixture_has_two_owners_for_cluster_three(self) -> None:
        image = build_fixture("cross-linked-chain")
        root = _root_offset()
        clusters = [
            struct.unpack_from("<H", image, root + offset + 26)[0]
            for offset in (0, 32)
        ]
        self.assertEqual([3, 3], clusters)


if __name__ == "__main__":
    unittest.main()
