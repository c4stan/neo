Windows dependencies:

	GnuWin32 packages:
		https://gnuwin32.sourceforge.net/packages/coreutils.htm
		https://gnuwin32.sourceforge.net/packages/make.htm
			this one is only needed for its dependencies, not for make itself. make comes from mingw
			TODO check if there's a better way to get those dependencies
		after running the installers manually add "Program Files (x86)\GnuWin32\bin" to PATH

	Mingw-w64 (for an updated version of make):
		https://github.com/niXman/mingw-builds-binaries/releases
			get the latest win32 ucrt version
		just copy paste the downloaded folder to C:\ and add mingw64\bin to PATH

	Clang for Windows:
		https://releases.llvm.org/download.html
		opt in for adding clang to PATH during install process

	Visual Studio (for debugging)
		only needs Desktop development with C++ (and probably less than all that's in there)
		TODO try other debuggers (rad debugger?)
