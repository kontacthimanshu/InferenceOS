# Disclaimer
InferenceOS is an operating system philosophy which has its roots in two of the world'd most prominent operating system philosophies as far as file system is concerned, namely Microsoft Windows and Linux. There is no intention to infringe on intellectual property and/or copyrights, goodwill of these operating systems.

#### Readily Usable Hyper-V VM
- Readily usable Hyper-V VM of this operating system is inside the folder:

  ./ReadilyUsableHyperV-VM

- Just import the VM and keep all the virtual machine related files in one folder.

# InferenceOS

InferenceOS is an experimental, open-source x86-64 operating-system which moves file-type knowledge away from the filename into an operating-system-controlled metadata and routing layer.

The defining experiment keeps an authoritative extension in each internal primary directory record and stores a separate extension-hash companion record. Ordinary CUI, GUI, and application views hide
both values (file type extention and file type extension hash); privileged diagnostics and raw-disk analysis can reveal them. 

Traditional operating systems commonly allow file-type information to become part of the filename or application-visible naming convention. In Windows, a filename such as report.docx explicitly carries its extension, and that extension participates heavily in application association. Windows Explorer may hide known extensions from the user, but the extension remains part of the filename and is readily available to applications.

Linux takes a more flexible approach. The Linux kernel does not fundamentally require filename extensions to determine what a file is. However, desktop environments and applications commonly rely on filename suffixes, MIME databases, file signatures, or combinations of these mechanisms to infer how a file should be handled.

## InferenceOS takes a different architectural approach.

A file's type information remains internal to the operating system and InferenceOS-FS, rather than forming part of the ordinary application-visible identity of the file. The user may see a file simply as:

REPORT

while InferenceOS internally retains its authoritative extension and the corresponding extension-hash companion metadata.

Applications therefore do not need to ask:

Does this filename end with .DOC?

or:

Give me every file matching *.DOC.

Applications just ask for any particular file using its extensionless name and InferenceOS returns the file. If the file happnes to be in a format which the application software can work upon, well and good. Otherwise the there is an application error stating "Unexpected file format", or whatever suitable user friendly error message the application software wants to gracefully degrade with.

This architecture can provide a narrower and more controlled security boundary.

An ordinary application does not need unrestricted access to the internal extension or extension hash. It receives files through operating-system-mediation which is a system call.

Consequently, applications have less opportunity to independently reinterpret filenames, incorrectly parse extensions, or make conflicting decisions about file type.

The approach also reduces the importance of filename-based deception. Names containing misleading combinations such as multiple extensions cannot be relied upon by applications as the authoritative declaration of file type, because InferenceOS would itself owns the type association if implemented as formal industry recognized operating system.

The extension hash provides additional derived metadata that the operating system can use for efficient classification or very fast lookup.

#### InferenceOS does not attempt to secure file types merely by hiding extensions. It secures the file-type relationship by placing interpretation, validation, and application routing behind an operating-system-controlled boundary.

## The distinction can be summarized as:

#### Windows: filename → extension → application association

#### Typical Linux desktop: filename/content → MIME/type inference → application

#### InferenceOS: file → OS-controlled type metadata → validated application capability → opaque file access

### It also gives the operating system one authoritative place to enforce which application is expected to receive which class of file.

# Security

Hiding extensions and using hash-based type lookup can reduce some attack surfaces, but it does not prevent hacking in general. Its value comes from changing who is allowed to know and interpret file type.

### The strongest security benefit comes with this rule:

#### Applications do not inspect extensions themselves. They ask the OS for files they are permitted or designed to handle, and the OS performs type resolution internally.

#### For Example:
### An application like Microsoft Word which for example can operate on or work on/with, file type extensions like: *.doc, *.docx, *.jpg, *.img, *.pdf etc etc.

Now since Microsoft Word knows that these are the file extensions it needs to work properly, it would only ask for files having these extensions from InferenceOS. And InferenceOS will search the InferenceOS-FS FAT file system by converting the file extensions requested into a hash and seach the InferenceOS file with some very fast hashing based searching algorithm rather than doing just string comparison. This brings performance gain. The performance gain has to be measured in different forms of computing servers based on InferenceOS would support.

## 1. It reduces filename-based deception

In conventional systems, users and applications frequently reason from names such as:

- invoice.pdf

- photo.jpg

- report.docx

Attackers can exploit that expectation with deceptive names:

- invoice.pdf.exe
- invoice.pdf.scr
- report.docx.exe

or with Unicode/look-alike tricks.

In InferenceOS, the ordinary user might simply see:

- Invoice

while internally the OS knows:

Internal extension: EXE

Extension hash: 6584638

Application class: Executable

The user-facing filename therefore cannot falsely advertise itself as .PDF, because the extension is not part of the presentation contract.

The OS determines the actual type.

#### This can eliminate an entire class of extension-spoofing social engineering.

## Malware cannot simply enumerate *.DOC, *.XLS, etc.

Consider malicious software trying to find valuable files.
On a conventional system, it might conceptually do:

- find *.docx
- find *.xlsx
- find *.pdf
- find *.key

## 3. Applications cannot arbitrarily ask for every interesting extension

Instead of allowing an application:

Give me all *.DOC files

InferenceOS could require:

Give application X the files it is registered to handle or it is asking for. Keeping a registry of application to extension mapping is optional and left open for pondering to OS implmenters.

Application

        |
        | "Give me my supported files"
        v

InferenceOS

        |
        +--> verifies application identity
        |
        +--> determines permitted file types
        |
        +--> searches internally
        |
        v
Opaque file handles

So malicious program MALWARE cannot simply request:
- *.DOC
- *.XLS
- *.PDF

because it never gets an API taking those extensions or enumerate filenames and inspect suffixes.

## It could reduce ransomware reconnaissance

Imagine ransomware trying to locate valuable files. Typical targets might include:

- .doc
- .docx
- .xls
- .xlsx
- .pdf
- .jpg
- .sql
- .db

Traditional ransomware can enumerate files and rank them immediately from their extensions.

On InferenceOS an unprivileged malicious application might instead see:

- Q4 Results
- Family Photos
- Customer Data
- Research

without extension/type metadata.

More importantly, InferenceOS enforces/limits that to work on a certain file type use the official API provided by the application software that creates that type of file. For example, use official OpenOffice API to work on *.docx, *.pptx, *.csv, *.xslx. This would be the approach taken by any custom application software or program for example: custom tools users have to develop in day to day office work.

## It can make application impersonation harder

Suppose a malicious application says:

#### “I support Excel documents. Give me every Excel file.”

InferenceOS does not have to trust that statement (when application to extention registry has been implemented).

Instead:

Application identity

            |
            v
Trusted application registry

            |
            +--> permitted TypeToken 14
            +--> permitted TypeToken 21
            |
            v

OS-controlled file enumeration

## Demonstrated scope

- CUI and GUI views over the same VFS namespace, including File Explorer and filtered type viewers (DOC Files, TXT Files);
  the command prompt remains in the standalone CUI rather than opening automatically in GUI mode.
- A distinct InferenceOS-FS volume with a sparse 64 GiB reference disk, durable save ordering,
  metadata validation, and reboot-persistence workflows.
- Shell-mediated, opaque application file services that do not expose raw extensions or hashes.
- Reproducible freestanding C17 builds with pinned GCC and Clang profiles.
- An optional Extension Registry research path that always falls back to authoritative directory
  metadata.

The Extension Registry is disabled by default. Benchmark evidence may make an implementation
proposal-eligible, but this README claims no performance improvement or default enablement.

InferenceOS is not production-ready, hardened, multi-user, network-complete, POSIX-compatible, or a
general-purpose replacement for an established operating system. InferenceOS-FS is not FAT32-compatible.

## Build and validation

Start with [docs/build.md](docs/build.md) and the
[feature quickstart](specs/001-inferenceos/quickstart.md). The exact supported boundary and deferred
capabilities are documented in [docs/limitations.md](docs/limitations.md). The final constitutional
and release-claim disposition is recorded in
[docs/validation/constitution-check.md](docs/validation/constitution-check.md).

InferenceOS is licensed under the [MIT License](LICENSE).

## Quick Virtual Machine Creation in Hyper-V

Clone the repository.
Open an elevated PowerShell window.
Run from elevated PowerShell:

  Set-Location C:\Users\konta\source\repos\InferenceOS

  & .\tools\image\recreate_hyperv_vm.ps1 `
      -Force `
      -Confirm:$false `
      -Start

## Mandatory File System Mounting

- After the InferenceOS has booted up and command prompt is visible. Run following commands to mount the InferenceOS-FS FAT disk.

#### format disk0
#### mount disk0 /

## Command and Feature Illustration Commands

The following commands are available at the `InferenceOS>` prompt. Arguments in angle brackets are
required, while arguments in square brackets are optional. Commands that use a displayed path expect
the extension-hidden name shown by `dir`. Diagnostic commands require the kernel-provided diagnostic
capability described in [the CUI documentation](docs/cui.md#privileged-diagnostic-commands).

| Command | Brief Description | Few Examples |
|---|---|---|
| `help [command]` | Lists every registered command, or shows the exact syntax and summary for one command. | `help`<br>`help write`<br>`help diskinfo` |
| `version` | Displays the current InferenceOS version. | `version` |
| `clear` | Clears the active text console. | `clear` |
| `gui` | Starts the graphical desktop; a startup failure leaves the CUI available. | `gui` |
| `devices` | Lists registered block devices and their status, sector geometry, and capacity. | `devices` |
| `diskinfo <diskN>` | Shows a block device's geometry, detected filesystem, classification, and mount state. | `diskinfo disk0`<br>`diskinfo disk1` |
| `format <diskN>` | Creates an InferenceOS-FS filesystem on an eligible unmounted device. This erases that device's existing filesystem data. | `format disk0`<br>`format disk1` |
| `mount <diskN> /` | Probes and mounts an InferenceOS-FS device as the VFS root. | `mount disk0 /`<br>`mount disk1 /` |
| `unmount /` | Flushes storage and unmounts the VFS root when no operation is active. | `unmount /` |
| `fsinfo` | Displays mounted-filesystem identity, capacity, geometry, free-space, hash, and registry information. | `fsinfo` |
| `sync` | Persists filesystem and cache state, then flushes the device. | `sync` |
| `reboot` | Synchronizes required storage state and requests a restart. | `reboot` |
| `shutdown` | Synchronizes required storage state and requests a halt or power-off. | `shutdown` |
| `create <path>` | Creates an empty regular file at a canonical typed path. | `create /DOCS/REPORT.TXT`<br>`create NOTES.TXT`<br>`create /IMAGES/LOGO.BMP` |
| `write <display-path> "<text>"` | Creates an extensionless text file, or initializes an existing empty file, using its displayed path. | `write NOTE "Hello"`<br>`write /DOCS/REPORT "persistent data"`<br>`write "/DOCS/REPORT (2)" "second copy"` |
| `append <display-path> "<text>"` | Appends supported text to an empty or content-validated text file. | `append NOTE " world"`<br>`append /DOCS/REPORT " and more"` |
| `type <path>` | Displays file content addressed by its canonical typed path. | `type /DOCS/REPORT.TXT`<br>`type NOTES.TXT` |
| `cat <display-path>` | Validates a complete file as supported text and displays it using its extension-hidden path. | `cat NOTE`<br>`cat /DOCS/REPORT`<br>`cat "/DOCS/REPORT (2)"` |
| `rename <display-source> <display-destination>` | Renames or moves a displayed regular file while preserving its hidden authoritative extension. | `rename NOTE MEMO`<br>`rename /DOCS/REPORT /DOCS/SUMMARY`<br>`rename /DOCS/NOTE /ARCHIVE/NOTE` |
| `delete <display-path>` | Deletes the exact regular file selected by its displayed path. | `delete NOTE`<br>`delete /DOCS/REPORT`<br>`delete "/DOCS/REPORT (2)"` |
| `search <extension>` | Recursively finds files with an exact extension and prints extension-hidden absolute paths. | `search DOC`<br>`search .TXT`<br>`search JPG` |
| `dir [path]` | Lists display-safe entries in the current directory or a supplied directory. | `dir`<br>`dir /DOCS`<br>`dir .` |
| `cd <path>` | Changes the calling console's current directory. | `cd /DOCS`<br>`cd ..`<br>`cd /` |
| `pwd` | Prints the current directory. | `pwd` |
| `mkdir <path>` | Creates a directory. | `mkdir /DOCS`<br>`mkdir ARCHIVE`<br>`mkdir /IMAGES` |
| `rmdir <path>` | Removes an empty directory. | `rmdir EMPTY`<br>`rmdir /DOCS/OLD` |
| `fileinfo <display-path>` | Shows privileged internal metadata for a file selected by its displayed path. | `fileinfo REPORT`<br>`fileinfo /DOCS/REPORT`<br>`fileinfo "/DOCS/REPORT (2)"` |
| `hashinfo <display-or-canonical-path>` | Shows privileged extension-hash values and companion-record validation state. | `hashinfo REPORT`<br>`hashinfo /DOCS/REPORT`<br>`hashinfo /DOCS/REPORT.TXT` |
| `fatinfo <display-or-canonical-path>` | Shows a privileged, bounded, validated cluster chain for a file. | `fatinfo REPORT`<br>`fatinfo /DOCS/REPORT`<br>`fatinfo /DOCS/REPORT.TXT` |
