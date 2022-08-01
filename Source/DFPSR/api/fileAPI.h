﻿// zlib open source license
//
// Copyright (c) 2020 to 2022 David Forsgren Piuva
// 
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
//    1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 
//    2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 
//    3. This notice may not be removed or altered from any source
//    distribution.

/*
TODO:
* Test that overwriting a large file with a smaller file does not leave anything from the overwritten file on any system.
* bool file_createFolder(const ReadableString& path);
	WINBASEAPI WINBOOL WINAPI CreateDirectoryW (LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
	mkdir on Posix
* bool file_remove(const ReadableString& path);
	WINBASEAPI WINBOOL WINAPI DeleteFileW (LPCWSTR lpFileName);
*/

#ifndef DFPSR_API_FILE
#define DFPSR_API_FILE

#include "../api/stringAPI.h"
#include "bufferAPI.h"
#if defined(WIN32) || defined(_WIN32)
	#define USE_MICROSOFT_WINDOWS
#endif

// A module for file access that exists to prevent cyclic dependencies between strings and buffers.
//   Buffers need a filename to be saved or loaded while strings use buffers to store their characters.
namespace dsr {
	// The PathSyntax enum allow processing theoreical paths for other operating systems than the local.
	enum class PathSyntax { Windows, Posix };
	#ifdef USE_MICROSOFT_WINDOWS
		// Let the local syntax be for Windows.
		#define LOCAL_PATH_SYNTAX PathSyntax::Windows
	#else
		// Let the local syntax be for Posix.
		#define LOCAL_PATH_SYNTAX PathSyntax::Posix
	#endif

	// Define NO_IMPLICIT_PATH_SYNTAX before including the header if you want all PathSyntax arguments to be explicit.
	// If a function you are calling adds a new pathSyntax argument, defining NO_IMPLICIT_PATH_SYNTAX will make sure that you get a warning from the compiler after upgrading the library.
	#ifdef NO_IMPLICIT_PATH_SYNTAX
		// No deafult argument for PathSyntax input.
		#define IMPLICIT_PATH_SYNTAX
	#else
		// Local deafult argument for PathSyntax input.
		#define IMPLICIT_PATH_SYNTAX = LOCAL_PATH_SYNTAX
	#endif

	// Path-syntax: According to the local computer.
	// Post-condition:
	//   Returns the content of the readable file referred to by file_optimizePath(filename).
	//   If mustExist is true, then failure to load will throw an exception.
	//   If mustExist is false, then failure to load will return an empty handle (returning false for buffer_exists).
	Buffer file_loadBuffer(const ReadableString& filename, bool mustExist = true);

	// Path-syntax: According to the local computer.
	// Side-effect: Saves buffer to file_optimizePath(filename) as a binary file.
	// Pre-condition: buffer exists.
	//   If mustWork is true, then failure to load will throw an exception.
	//   If mustWork is false, then failure to load will return false.
	// Post-condition: Returns true iff the buffer could be saved as a file.
	bool file_saveBuffer(const ReadableString& filename, Buffer buffer, bool mustWork = true);

	// Path-syntax: According to the local computer.
	// Pre-condition: file_getEntryType(path) == EntryType::SymbolicLink
	// Post-condition: Returns the destination of a symbolic link as an absolute path.
	// Shortcuts with file extensions are counted as files, not links.
	// TODO: Should shortcuts of known formats be supported anyway by parsing them?
	String file_followSymbolicLink(const ReadableString &path, bool mustExist = true);

	// Path-syntax: According to the local computer.
	// Get a path separator for the target operating system.
	//   Can be used to construct a file path that works for both forward and backward slash separators.
	const char32_t* file_separator();

	// Path-syntax: Depends on pathSyntax argument.
	// Turns / and \ into the path convention specified by pathSyntax, which is the local system's by default.
	// Removes redundant . and .. to reduce the risk of running out of buffer space when calling the system.
	String file_optimizePath(const ReadableString &path, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);

	// Path-syntax: Depends on pathSyntax argument.
	// Combines two parts into a path and automatically adding a local separator when needed.
	// Can be used to get the full path of a file in a folder or add another folder to the path.
	// b may not begin with a separator, because only a is allowed to contain the root.
	String file_combinePaths(const ReadableString &a, const ReadableString &b, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);

	// Path-syntax: Depends on pathSyntax argument.
	// Post-condition: Returns true for relative paths true iff path contains a root, according to the path syntax.
	//                 Implicit drives on Windows using \ are treated as roots because we know that there is nothing above them.
	// If treatHomeFolderAsRoot is true, starting from the /home/username folder using the Posix ~ alias will be allowed as a root as well, because we can't append it behind another path.
	bool file_hasRoot(const ReadableString &path, bool treatHomeFolderAsRoot, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);

	// Path-syntax: Depends on pathSyntax argument.
	// Returns true iff path is a root without any files nor folder names following.
	//   Does not check if it actually exists, so use file_getEntryType on the actual folders and files for verifying existence.
	bool file_isRoot(const ReadableString &path, bool treatHomeFolderAsRoot, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);

	// DSR_MAIN_CALLER is a convenient wrapper for getting input arguments as a list of portable Unicode strings.
	//   The actual main function gets placed in DSR_MAIN_CALLER, which calls the given function.
	// Example:
	//   DSR_MAIN_CALLER(dsrMain)
	//   void dsrMain(List<String> args) {
	//       printText("Input arguments:\n");
	//       for (int a = 0; a < args.length(); a++) {
	//           printText("  args[", a, "] = ", args[a], "\n");
	//       }
	//   }
	#ifdef USE_MICROSOFT_WINDOWS
		#define DSR_MAIN_CALLER(MAIN_NAME) \
			void MAIN_NAME(List<String> args); \
			int main() { MAIN_NAME(file_impl_getInputArguments()); return 0; }
	#else
		#define DSR_MAIN_CALLER(MAIN_NAME) \
			void MAIN_NAME(List<String> args); \
			int main(int argc, char **argv) { MAIN_NAME(file_impl_convertInputArguments(argc, (void**)argv)); return 0; }
	#endif
	// Helper functions have to be exposed for the macro handle your input arguments.
	//   Do not call these yourself.
	List<String> file_impl_convertInputArguments(int argn, void **argv);
	List<String> file_impl_getInputArguments();

	// Path-syntax: According to the local computer.
	// Post-condition: Returns the current path, from where the application was called and relative paths start.
	String file_getCurrentPath();

	// Path-syntax: According to the local computer.
	// Side-effects: Sets the current path to file_optimizePath(path).
	// Post-condition: Returns Returns true on success and false on failure.
	bool file_setCurrentPath(const ReadableString &path);

	// Path-syntax: According to the local computer.
	// Post-condition: Returns  the application's folder path, from where the application is stored.
	// If not implemented and allowFallback is true,
	//   the current path is returned instead as a qualified guess instead of raising an exception.
	String file_getApplicationFolder(bool allowFallback = true);

	// Path-syntax: This trivial operation should work the same independent of operating system.
	//              Otherwise you just have to add a new argument after upgrading the static library.
	// Returns the local name of the file or folder after the last path separator, or the whole path if no separator was found.
	ReadableString file_getPathlessName(const ReadableString &path);

	// Quickly gets the relative parent folder by removing the last entry from the string or appending .. at the end.
	// Path-syntax: Depends on pathSyntax argument.
	// This pure syntax function getting the parent folder does not access the system in any way.
	// Does not guarantee that the resulting path is usable on the system.
	// It allows using ~ as the root, for writing paths compatible across different user accounts pointing to different but corresponding files.
	// Going outside of a relative start will add .. to the path.
	//   Depending on which current directory the result is applied to, the absolute path may end up as a root followed by multiple .. going nowhere.
	// Going outside of the absolute root returns U"?" as an error code.
	String file_getRelativeParentFolder(const ReadableString &path, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);

	// Gets the canonical parent folder using the current directory.
	// This function for getting the parent folder treats path relative to the current directory and expands the result into an absolute path.
	// Make sure that current directory is where you want it when calling this function, because the current directory may change over time when calling file_setCurrentPath.
	// Path-syntax: According to the local computer.
	// Pre-conditions:
	//   path must be valid on the local system, such that you given full permissions could read or create files there relative to the current directory when this function is called.
	//   ~ is not allowed as the root, because the point of using ~ is to reuse the same path across different user accounts, which does not refer to an absolute home directory.
	// Post-condition: Returns the absolute parent to the given path, or U"?" if trying to leave the root or use a tilde home alias.
	String file_getAbsoluteParentFolder(const ReadableString &path);

	// Gets the canonical absolute version of the path.
	// Path-syntax: According to the local computer.
	// Post-condition: Returns an absolute version of the path, quickly without removing redundancy.
	String file_getAbsolutePath(const ReadableString &path);

	// Path-syntax: According to the local computer.
	// Pre-condition: filename must refer to a file so that file_getEntryType(filename) == EntryType::File.
	// Post-condition: Returns a structure with information about the file at file_optimizePath(filename), or -1 if no such file exists.
	int64_t file_getFileSize(const ReadableString& filename);

	// Entry types distinguish between files folders and other things in the file system.
	enum class EntryType { NotFound, UnhandledType, File, Folder, SymbolicLink };
	String& string_toStreamIndented(String& target, const EntryType& source, const ReadableString& indentation);

	// Path-syntax: According to the local computer.
	// Post-condition: Returns what the file_optimizePath(path) points to in the filesystem.
	// Different comparisons on the result can be used to check if something exists.
	//   Use file_getEntryType(filename) == EntryType::File to check if a file exists.
	//   Use file_getEntryType(folderPath) == EntryType::Folder to check if a folder exists.
	//   Use file_getEntryType(path) != EntryType::NotFound to check if the path leads to anything.
	EntryType file_getEntryType(const ReadableString &path);

	// Path-syntax: According to the local computer.
	// Side-effects: Calls action with the entry's path, name and type for everything detected in folderPath.
	//               entryPath equals file_combinePaths(folderPath, entryName), and is used for recursive calls when entryType == EntryType::Folder.
	//               entryName equals file_getPathlessName(entryPath).
	//               entryType equals file_getEntryType(entryPath).
	// Post-condition: Returns true iff the folder could be found.
	bool file_getFolderContent(const ReadableString& folderPath, std::function<void(const ReadableString& entryPath, const ReadableString& entryName, EntryType entryType)> action);



	// Functions below are for testing and simulation of other systems by substituting the current directory and operating system with manual settings.

	// A theoretical version of file_getParentFolder for evaluation on a theoretical system without actually calling file_getCurrentPath or running on the given system.
	// Path-syntax: Depends on pathSyntax argument.
	// Post-condition: Returns the absolute parent to the given path, or U"?" if trying to leave the root or use a tilde home alias.
	String file_getTheoreticalAbsoluteParentFolder(const ReadableString &path, const ReadableString &currentPath, PathSyntax pathSyntax);

	// A theoretical version of for evaluation on a theoretical system without actually calling file_getCurrentPath or running on the given system.
	// Path-syntax: Depends on pathSyntax argument.
	// Post-condition: Returns an absolute version of the path, quickly without removing redundancy.
	String file_getTheoreticalAbsolutePath(const ReadableString &path, const ReadableString &currentPath, PathSyntax pathSyntax IMPLICIT_PATH_SYNTAX);
}

#endif
