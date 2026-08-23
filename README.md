# InferenceOS

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
