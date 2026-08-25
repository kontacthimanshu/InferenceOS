Disclaimer: This operating system is based on two of the most prominent operating system approaches called Windows and Linux. There is no intention to infringe on the intellectual properties of both the operating systems.

# InferenceOS
It is called InferenceOS because only application program can infer the files it operates on.

# Motivation
There are just two approaches of doing an operating system from its file system's perspective. Those are as follows:
- OS having file extension driven file system, for example Microsoft Windows
- OS having file system wherein file extension is absent from file name, for example Linux

I am proposing third approach wherein file names have extensions but the extensions are just for display perspective. The idea is not to challenge the above two approaches but to make a small tweak in the way these two approaches do file system.

Under the first approach the file name extension let's the OS decide which application can work on this file and let the application work on it without inspecting about the type/content of the file. Under the second approach since the extension is absent there is no way for the operating system/application program to know what is the type or content of the file thereby the operating system/application program uses different means to know the type of content like:

- potentially inspecting content of the file (header(s)), to display user friendly icons in Linux and launch application which can work on it, which is costly operation even if it is small.

Under the first approach the operating system has exposed the type and potential security risk because when you know the type of the file, you can make programs to hack the contents of the file.

My approach is not an absolutely new approach as far as file system is concerned. It brings about an improvement in the first approach wherein it treats the extension of the file just for display purpose.

# Optimizations
Listed below are the optimizations this approach brings about:
- When the application program does not have to worry about inspecting file header to infer the type, content and format of the file, the application program improves a lot and becomes faster 

# Security Benefits

# Application Programs Improvements
- OOB Application programs will ask OS API about files they are capable of working on.
- When OOB application programs would write the file to the disk, the operating system would register every new file extension to its file extension registry.
-  
# OS File Explorer Improvements
- OS File explorers will not show the extension of the file.

# Use Cases Challenged by this approach and solutions
- Searches based on file extensions such grep, windowed file explorer searches are not possible because OS will not expose what type (*.exe, *.docx) of file is saved in its file system.
- Only custom applications which operate on different type of files such as (*.docx, &xslx, *PDF etc) will have to rely on format of the file to work on it. Every major proprietary or open source software create API to work on its files, so custom programs can work on OOB file formats as well as open source software based file formats using their respective API, otherwise no. If we don't place this much strictness then our OSes will not be secure. 
