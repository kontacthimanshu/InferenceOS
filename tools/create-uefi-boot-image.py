#!/usr/bin/env python3
"""Create the deterministic FAT32 superfloppy used to boot InferenceOS."""

from __future__ import annotations

import argparse
import datetime as dt
import math
import struct
from pathlib import Path

SECTOR_SIZE = 512
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2
EFI_CLUSTER = 3
BOOT_CLUSTER = 4
FIRST_FILE_CLUSTER = 5
END_OF_CHAIN = 0x0FFFFFFF


def _fat_datetime(epoch: int) -> tuple[int, int]:
    instant = dt.datetime.fromtimestamp(max(epoch, 315532800), tz=dt.timezone.utc)
    if instant.year > 2107:
        instant = instant.replace(year=2107, month=12, day=31, hour=23, minute=59, second=58)
    fat_date = ((instant.year - 1980) << 9) | (instant.month << 5) | instant.day
    fat_time = (instant.hour << 11) | (instant.minute << 5) | (instant.second // 2)
    return fat_date, fat_time


def _directory_entry(
    name: bytes, attributes: int, first_cluster: int, size: int, fat_date: int, fat_time: int
) -> bytes:
    if len(name) != 11 or not 0 <= size <= 0xFFFFFFFF:
        raise ValueError("invalid FAT directory entry")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    struct.pack_into("<H", entry, 14, fat_time)
    struct.pack_into("<H", entry, 16, fat_date)
    struct.pack_into("<H", entry, 18, fat_date)
    struct.pack_into("<H", entry, 22, fat_time)
    struct.pack_into("<H", entry, 24, fat_date)
    struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def _boot_sector(total_sectors: int, fat_sectors: int, volume_id: int) -> bytes:
    sector = bytearray(SECTOR_SIZE)
    sector[0:3] = b"\xEB\x58\x90"
    sector[3:11] = b"INFEROS "
    struct.pack_into("<H", sector, 11, SECTOR_SIZE)
    sector[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", sector, 14, RESERVED_SECTORS)
    sector[16] = FAT_COUNT
    struct.pack_into("<H", sector, 17, 0)
    struct.pack_into("<H", sector, 19, 0)
    sector[21] = 0xF8
    struct.pack_into("<H", sector, 22, 0)
    struct.pack_into("<H", sector, 24, 63)
    struct.pack_into("<H", sector, 26, 255)
    struct.pack_into("<I", sector, 28, 0)
    struct.pack_into("<I", sector, 32, total_sectors)
    struct.pack_into("<I", sector, 36, fat_sectors)
    struct.pack_into("<H", sector, 40, 0)
    struct.pack_into("<H", sector, 42, 0)
    struct.pack_into("<I", sector, 44, ROOT_CLUSTER)
    struct.pack_into("<H", sector, 48, 1)
    struct.pack_into("<H", sector, 50, 6)
    sector[64] = 0x80
    sector[66] = 0x29
    struct.pack_into("<I", sector, 67, volume_id)
    sector[71:82] = b"INFERENCEOS"
    sector[82:90] = b"FAT32   "
    sector[510:512] = b"\x55\xAA"
    return bytes(sector)


def _fsinfo(free_clusters: int, next_free: int) -> bytes:
    sector = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", sector, 0, 0x41615252)
    struct.pack_into("<I", sector, 484, 0x61417272)
    struct.pack_into("<I", sector, 488, free_clusters)
    struct.pack_into("<I", sector, 492, next_free)
    struct.pack_into("<I", sector, 508, 0xAA550000)
    return bytes(sector)


def _fat_sectors(total_sectors: int) -> tuple[int, int]:
    for sectors in range(1, total_sectors):
        data_sectors = total_sectors - RESERVED_SECTORS - FAT_COUNT * sectors
        if data_sectors <= 0:
            break
        clusters = data_sectors // SECTORS_PER_CLUSTER
        if sectors * (SECTOR_SIZE // 4) >= clusters + 2:
            return sectors, clusters
    raise ValueError("unable to derive a valid FAT32 geometry")


def _clusters_for(size: int) -> int:
    return max(1, math.ceil(size / (SECTOR_SIZE * SECTORS_PER_CLUSTER)))


def create_image(
    output: Path, loader: Path, kernel: Path, image_size: int, volume_id: int, epoch: int
) -> None:
    if image_size % SECTOR_SIZE != 0 or image_size < 32 * 1024 * 1024:
        raise ValueError("boot image must be sector-aligned and at least 32 MiB")
    loader_data = loader.read_bytes()
    kernel_data = kernel.read_bytes()
    if not loader_data or not kernel_data:
        raise ValueError("loader and kernel inputs must be non-empty")

    total_sectors = image_size // SECTOR_SIZE
    fat_sectors, cluster_count = _fat_sectors(total_sectors)
    if cluster_count < 65525:
        raise ValueError("geometry does not qualify as FAT32")
    first_data_sector = RESERVED_SECTORS + FAT_COUNT * fat_sectors
    loader_clusters = _clusters_for(len(loader_data))
    kernel_clusters = _clusters_for(len(kernel_data))
    loader_first = FIRST_FILE_CLUSTER
    kernel_first = loader_first + loader_clusters
    first_free = kernel_first + kernel_clusters
    if first_free - 2 > cluster_count:
        raise ValueError("loader and kernel do not fit in the boot image")

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
    struct.pack_into("<I", fat, 4, END_OF_CHAIN)
    for cluster in (ROOT_CLUSTER, EFI_CLUSTER, BOOT_CLUSTER):
        struct.pack_into("<I", fat, cluster * 4, END_OF_CHAIN)
    for first, count in ((loader_first, loader_clusters), (kernel_first, kernel_clusters)):
        for offset in range(count):
            value = END_OF_CHAIN if offset + 1 == count else first + offset + 1
            struct.pack_into("<I", fat, (first + offset) * 4, value)

    fat_date, fat_time = _fat_datetime(epoch)
    root = bytearray(SECTOR_SIZE)
    root[0:32] = _directory_entry(b"INFERENCEOS", 0x08, 0, 0, fat_date, fat_time)
    root[32:64] = _directory_entry(b"EFI        ", 0x10, EFI_CLUSTER, 0, fat_date, fat_time)
    root[64:96] = _directory_entry(
        b"KERNEL  ELF", 0x20, kernel_first, len(kernel_data), fat_date, fat_time
    )
    efi_dir = bytearray(SECTOR_SIZE)
    efi_dir[0:32] = _directory_entry(b".          ", 0x10, EFI_CLUSTER, 0, fat_date, fat_time)
    efi_dir[32:64] = _directory_entry(b"..         ", 0x10, ROOT_CLUSTER, 0, fat_date, fat_time)
    efi_dir[64:96] = _directory_entry(b"BOOT       ", 0x10, BOOT_CLUSTER, 0, fat_date, fat_time)
    boot_dir = bytearray(SECTOR_SIZE)
    boot_dir[0:32] = _directory_entry(b".          ", 0x10, BOOT_CLUSTER, 0, fat_date, fat_time)
    boot_dir[32:64] = _directory_entry(b"..         ", 0x10, EFI_CLUSTER, 0, fat_date, fat_time)
    boot_dir[64:96] = _directory_entry(
        b"BOOTX64 EFI", 0x20, loader_first, len(loader_data), fat_date, fat_time
    )

    allocated_clusters = 3 + loader_clusters + kernel_clusters
    info = _fsinfo(cluster_count - allocated_clusters, first_free)
    boot_sector = _boot_sector(total_sectors, fat_sectors, volume_id)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    with temporary.open("wb") as image:
        image.truncate(image_size)
        for sector, data in ((0, boot_sector), (1, info), (6, boot_sector), (7, info)):
            image.seek(sector * SECTOR_SIZE)
            image.write(data)
        for copy in range(FAT_COUNT):
            image.seek((RESERVED_SECTORS + copy * fat_sectors) * SECTOR_SIZE)
            image.write(fat)

        def write_cluster(cluster: int, data: bytes) -> None:
            sector = first_data_sector + (cluster - 2) * SECTORS_PER_CLUSTER
            image.seek(sector * SECTOR_SIZE)
            image.write(data)

        write_cluster(ROOT_CLUSTER, root)
        write_cluster(EFI_CLUSTER, efi_dir)
        write_cluster(BOOT_CLUSTER, boot_dir)
        write_cluster(loader_first, loader_data)
        write_cluster(kernel_first, kernel_data)
    temporary.replace(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--loader", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--size", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--volume-id", type=lambda value: int(value, 0), default=0x494E464F)
    parser.add_argument("--epoch", type=int, default=0)
    args = parser.parse_args()
    if args.epoch < 0:
        parser.error("--epoch must be non-negative")
    create_image(args.output.resolve(), args.loader.resolve(), args.kernel.resolve(),
                 args.size, args.volume_id, args.epoch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
