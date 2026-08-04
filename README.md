# InferenceOS
(World's most secure operating system)

# Motivation
There are just two philosophical views of an operating system from its file system's perspective. Those are as follows:
- OS having file extension driven file system, for example Microsoft Windows
- OS having file system wherein file extension is absent from file name, for example Linux

I am proposing third philosophy wherein file names have extensions but the extensions are just for display perspective. The idea is not to challenge the above two philosophies but to make a small tweak in the way these two philosophies approach file system.

Under the first philosophy the file name extension let's the OS decide which application can work on this file and let the application work on it without inspecting about the type/content of the file. Under the second philosophy since the extension is absent there is no way for the operating system to know what is the type or content of the file thereby the operating system uses different means to like:
- potentially inspecting content of the file, to display user friendly icons in Windows and launch application which can work on it, which is costly operation even if it is small. Cost is cost, time is money.

Under the first philosophy the operating system has exposed the type and potential security risk because when you know the type of the file, you can make programs to hack the contents of the file.
