#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly REPOSITORY_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd -P)"
readonly VERSION_FILE="${SCRIPT_DIR}/versions.json"

install_root="${INFERENCEOS_INSTALL_ROOT:-${HOME}/.local/share/inferenceos/tools}"
download_root="${INFERENCEOS_DOWNLOAD_ROOT:-${XDG_CACHE_HOME:-${HOME}/.cache}/inferenceos-bootstrap}"
environment_file=""
check_only=0

usage() {
    echo "Usage: $0 [--check] [--install-root PATH] [--download-root PATH] [--env-file PATH]"
}

while (($#)); do
    case "$1" in
        --check) check_only=1; shift ;;
        --install-root) install_root="${2:?missing --install-root value}"; shift 2 ;;
        --download-root) download_root="${2:?missing --download-root value}"; shift 2 ;;
        --env-file) environment_file="${2:?missing --env-file value}"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

environment_file="${environment_file:-${install_root}/environment.sh}"
readonly prefix="${install_root}/prefix"
readonly source_root="${download_root}/sources"
readonly build_root="${download_root}/build"
readonly jobs="${INFERENCEOS_BOOTSTRAP_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

[[ -f "${VERSION_FILE}" ]] || { echo "error: missing ${VERSION_FILE}" >&2; exit 1; }

json_value() {
    python3 - "${VERSION_FILE}" "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    data = json.load(stream)
print(data["components"][sys.argv[2]][sys.argv[3]])
PY
}

version() { json_value "$1" version; }
source_url() { json_value "$1" source; }

readonly binutils_version="$(version binutils)"
readonly gcc_version="$(version gcc)"
readonly llvm_version="$(version llvm)"
readonly qemu_version="$(version qemu)"
readonly ovmf_version="$(version ovmf)"
readonly cmake_version="$(version cmake)"
readonly ninja_version="$(version ninja)"

export PATH="${prefix}/bin:${PATH}"

command_version() {
    local component="$1" command_name="$2" expected="$3" actual=""
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        printf '%-10s missing (%s)\n' "${component}" "${command_name}"
        return 1
    fi
    case "${component}" in
        gcc) actual="$(${command_name} -dumpfullversion -dumpversion 2>/dev/null | head -n1)" ;;
        ninja) actual="$(${command_name} --version 2>/dev/null | head -n1)" ;;
        *) actual="$(${command_name} --version 2>/dev/null | head -n1)" ;;
    esac
    if [[ "${actual}" != *"${expected}"* ]]; then
        printf '%-10s mismatch: expected %s, found %s\n' "${component}" "${expected}" "${actual}"
        return 1
    fi
    printf '%-10s %s\n' "${component}" "${expected}"
}

ovmf_version_ok() {
    local marker="${prefix}/share/ovmf/INFERENCEOS_OVMF_VERSION"
    [[ -f "${marker}" ]] && [[ "$(<"${marker}")" == "${ovmf_version}" ]] &&
        [[ -f "${prefix}/share/ovmf/OVMF_CODE.fd" ]] && [[ -f "${prefix}/share/ovmf/OVMF_VARS.fd" ]]
}

validate_all() {
    local failed=0
    command_version binutils x86_64-elf-as "${binutils_version}" || failed=1
    command_version gcc x86_64-elf-gcc "${gcc_version}" || failed=1
    command_version llvm clang "${llvm_version}" || failed=1
    command_version llvm ld.lld "${llvm_version}" || failed=1
    command_version qemu qemu-system-x86_64 "${qemu_version}" || failed=1
    command_version cmake cmake "${cmake_version}" || failed=1
    command_version ninja ninja "${ninja_version}" || failed=1
    if ovmf_version_ok; then
        printf '%-10s %s\n' ovmf "${ovmf_version}"
    else
        printf '%-10s missing or mismatched (%s)\n' ovmf "${ovmf_version}"
        failed=1
    fi
    return "${failed}"
}

if ((check_only)); then
    validate_all
    exit $?
fi

if [[ "$(uname -s)" != Linux ]]; then
    echo "error: the Linux bootstrap must run inside WSL Ubuntu or a compatible Ubuntu host" >&2
    exit 1
fi
if ! command -v apt-get >/dev/null 2>&1; then
    echo "error: apt-get is required; run this script in an Ubuntu distribution" >&2
    exit 1
fi

sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential bison flex texinfo libgmp-dev libmpc-dev libmpfr-dev libisl-dev \
    python3 python3-venv python3-pip git curl xz-utils gzip tar unzip pkg-config \
    libglib2.0-dev libpixman-1-dev libslirp-dev nasm acpica-tools uuid-dev \
    libncurses-dev libssl-dev

mkdir -p "${prefix}" "${source_root}" "${build_root}" "$(dirname -- "${environment_file}")"

fetch_and_extract() {
    local component="$1" archive="$2" directory="$3" url
    url="$(source_url "${component}")"
    if [[ ! -f "${archive}" ]]; then
        curl --fail --location --retry 3 --output "${archive}.partial" "${url}"
        mv -- "${archive}.partial" "${archive}"
    fi
    if [[ ! -d "${directory}" ]]; then
        mkdir -p "${directory}"
        tar -xf "${archive}" --strip-components=1 -C "${directory}"
    fi
}

build_binutils() {
    command_version binutils x86_64-elf-as "${binutils_version}" && return
    local src="${source_root}/binutils-${binutils_version}" build="${build_root}/binutils-${binutils_version}"
    fetch_and_extract binutils "${download_root}/binutils-${binutils_version}.tar.xz" "${src}"
    rm -rf -- "${build}"; mkdir -p "${build}"; pushd "${build}" >/dev/null
    "${src}/configure" --target=x86_64-elf --prefix="${prefix}" --disable-nls --disable-werror
    make -j"${jobs}"; make install; popd >/dev/null
}

build_gcc() {
    command_version gcc x86_64-elf-gcc "${gcc_version}" && return
    local src="${source_root}/gcc-${gcc_version}" build="${build_root}/gcc-${gcc_version}"
    fetch_and_extract gcc "${download_root}/gcc-${gcc_version}.tar.xz" "${src}"
    rm -rf -- "${build}"; mkdir -p "${build}"; pushd "${build}" >/dev/null
    "${src}/configure" --target=x86_64-elf --prefix="${prefix}" --disable-nls \
        --enable-languages=c --without-headers --disable-hosted-libstdcxx
    make -j"${jobs}" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc; popd >/dev/null
}

build_ninja() {
    command_version ninja ninja "${ninja_version}" && return
    local src="${source_root}/ninja-${ninja_version}"
    fetch_and_extract ninja "${download_root}/ninja-${ninja_version}.tar.gz" "${src}"
    pushd "${src}" >/dev/null; python3 configure.py --bootstrap
    install -m 0755 ninja "${prefix}/bin/ninja"; popd >/dev/null
}

build_cmake() {
    command_version cmake cmake "${cmake_version}" && return
    local src="${source_root}/cmake-${cmake_version}" build="${build_root}/cmake-${cmake_version}"
    fetch_and_extract cmake "${download_root}/cmake-${cmake_version}.tar.gz" "${src}"
    rm -rf -- "${build}"; mkdir -p "${build}"; pushd "${build}" >/dev/null
    "${src}/bootstrap" --prefix="${prefix}" --parallel="${jobs}"
    make -j"${jobs}"; make install; popd >/dev/null
}

build_llvm() {
    command_version llvm clang "${llvm_version}" && command_version llvm ld.lld "${llvm_version}" && return
    local src="${source_root}/llvm-${llvm_version}" build="${build_root}/llvm-${llvm_version}"
    fetch_and_extract llvm "${download_root}/llvm-project-${llvm_version}.tar.xz" "${src}"
    rm -rf -- "${build}"; mkdir -p "${build}"
    cmake -S "${src}/llvm" -B "${build}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${prefix}" -DLLVM_ENABLE_PROJECTS="clang;lld" \
        -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
    cmake --build "${build}" --parallel "${jobs}" --target install
}

build_qemu() {
    command_version qemu qemu-system-x86_64 "${qemu_version}" && return
    local src="${source_root}/qemu-${qemu_version}" build="${build_root}/qemu-${qemu_version}"
    fetch_and_extract qemu "${download_root}/qemu-${qemu_version}.tar.xz" "${src}"
    rm -rf -- "${build}"; mkdir -p "${build}"; pushd "${build}" >/dev/null
    "${src}/configure" --prefix="${prefix}" --target-list=x86_64-softmmu \
        --disable-werror --disable-docs
    make -j"${jobs}"; make install; popd >/dev/null
}

build_ovmf() {
    ovmf_version_ok && return
    local src="${source_root}/edk2-${ovmf_version}"
    if [[ ! -d "${src}/.git" ]]; then
        rm -rf -- "${src}"
        git clone --depth 1 --branch "${ovmf_version}" "$(source_url ovmf)" "${src}"
        git -C "${src}" submodule update --init --recursive --depth 1
    fi
    pushd "${src}" >/dev/null
    make -C BaseTools -j"${jobs}"
    # edksetup.sh reads optional environment variables before assigning their
    # defaults, so isolate it from this script's nounset mode.
    export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
    set +u
    # shellcheck disable=SC1091
    source edksetup.sh
    set -u
    build -a X64 -t GCC -b RELEASE -p OvmfPkg/OvmfPkgX64.dsc
    install -d "${prefix}/share/ovmf"
    install -m 0644 Build/OvmfX64/RELEASE_GCC/FV/OVMF_CODE.fd "${prefix}/share/ovmf/OVMF_CODE.fd"
    install -m 0644 Build/OvmfX64/RELEASE_GCC/FV/OVMF_VARS.fd "${prefix}/share/ovmf/OVMF_VARS.fd"
    printf '%s\n' "${ovmf_version}" >"${prefix}/share/ovmf/INFERENCEOS_OVMF_VERSION"
    popd >/dev/null
}

build_binutils
build_gcc
build_ninja
build_cmake
build_llvm
build_qemu
build_ovmf

validate_all

temporary_environment="${environment_file}.tmp.$$"
{
    echo '# Generated by tools/bootstrap/wsl-ubuntu.sh. Source this file explicitly.'
    printf 'export INFERENCEOS_TOOL_ROOT=%q\n' "${prefix}"
    printf 'export INFERENCEOS_TARGET=%q\n' 'x86_64-elf'
    printf 'export INFERENCEOS_OVMF_CODE=%q\n' "${prefix}/share/ovmf/OVMF_CODE.fd"
    printf 'export INFERENCEOS_OVMF_VARS=%q\n' "${prefix}/share/ovmf/OVMF_VARS.fd"
    printf 'export PATH=%q:"${PATH}"\n' "${prefix}/bin"
} >"${temporary_environment}"
chmod 0644 "${temporary_environment}"
mv -- "${temporary_environment}" "${environment_file}"

echo "InferenceOS toolchain is ready."
echo "Source the environment for this shell with: source '${environment_file}'"
