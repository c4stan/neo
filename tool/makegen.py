import os
import uuid
import xml.dom.minidom
import distutils.version
import shutil
import ntpath
import pprint
import configparser
import sys
import platform

if platform.system() == 'Windows':
    import win32pipe, win32file, pywintypes

# -----------------------------------------------------------------------------

# -- pipe
BUILD_CHANGES_OUTPUT_PIPE_NAME = 'std_module_update_pipe'

# -- WINNT target version
# https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt
WINNT_VERSION = '0x0A000005' # win10

# -- Warnings
CORE_WARNINGS_FLAGS = (
    '-Wno-switch-enum'                          # Having a _COUNT enum not explicitly handled in a switch throws this
    ' -Wno-gnu-zero-variadic-macro-arguments'   # Allow 0 args variadic macros and ending ', ##__VA_ARGS__' trick (std_log uses this extensively)
    ' -Wno-reserved-id-macro'                   # Allow e.g. names starting with __. Although not using it is probably a good idea, the Win32 API does, so.
    #' -Wno-strict-prototypes'                   # Allow declaring foo() without having to explicit the (void).
    #' -Wno-missing-prototypes'                  # Also complains about missing prototypes on 0 params functions not declared as (void)
    ' -Wno-cast-qual'                           # Allow casting away const
    ' -Wno-cast-align'                          # Allow casting from different alignments. Needed e.g. when casting an aligned allocation to the actual type.
    ' -Wno-covered-switch-default'              # Allow having a default switch tag even after having handled all declared values of e.g. an enum, to catch weird values
    ' -Wno-nonportable-system-include-path'     # Vulkan headers throw this for windows.h
    ' -Wno-gnu-statement-expression'            # Allow multiline macros to return values, more similarly to how an inlined functions would behave
    ' -Wno-assign-enum'                         # Allow to composite flag enums to form values that were not explicitly declared
    ' -Wno-format-security'                     # Allow to pass a non-literal to printf
    ' -Wno-format-nonliteral'                   # Allow to pass a non-literal to vsprintf
    ' -Wno-double-promotion'                    # printing floats promotes them automatically to doubles and throws a warning if it's implicit
    ' -Wno-parentheses'
    ' -Wno-undef'                               # Allow treating undefined macros as 0 (C99 compliant)
    ' -Wno-float-equal'                         # Don't warn when comparing float values with ==
    ' -Wfatal-errors'                           # Stop after first error
    ' -Wno-disabled-macro-expansion'
    ' -Wno-gnu-designator'                      # allow for int array[100] = { [0...99] = 1 };
    #' -Wno-void-pointer-to-enum-cast'           # allow casting void* to enum
    ' -Wno-initializer-overrides'               # allow assigning same member multiple times in initializer lists. Used in constructor macros for optional non-default params
    ' -Wno-unused-but-set-variable'
    ' -Wno-comment'
    ' -Wno-unused-value'                        # allows ignoring the result of an expression (e.g. a comparison), useful e.g. when using std_verify_m to check the return value of a function call
    ' -Wno-missing-braces'                      # suggested braces warnings?
    ' -Wno-assign-enum'                         # only exists on CLANG
)

EXTENDED_WARNINGS_FLAGS = (
    '-Werror'                                   # Treat warnings as errors
)

# -- Build output types
OUTPUT_LIB = 1
OUTPUT_DLL = 2
OUTPUT_EXE = 3
OUTPUT_INL = 4
OUTPUT_APP = 5 # DLL + std_launcher

# -- Build flags. Can be passed in when creating the Generator
BUILD_FLAG_NONE                     =  0
BUILD_FLAG_VERBOSE                  =  1
BUILD_FLAG_OUTPUT_PP                =  2
BUILD_FLAG_OUTPUT_ASM               =  4
BUILD_FLAG_OPTIMIZATION_OFF         =  8
BUILD_FLAG_PERMISSIVE_WARNINGS      = 16
BUILD_FLAG_RELOAD                   = 32

BUILD_FLAGS = BUILD_FLAG_NONE

# -- Config
CONFIG_DEBUG = 1    # default
CONFIG_RELEASE = 2  # -opt

# -- Custom Make targets
TARGET_ALL = 1
TARGET_CLEAN = 2

# -- Compilers
COMPILER_CLANG = 1
COMPILER_GCC = 2

DEFAULT_CUSTOM_TARGETS = [TARGET_ALL]#, TARGET_CLEAN]

"""
    todos:
        try to output all dlls from the makefile directly into the main project workspace instead of having to manually move them over via gather_dlls. delete modules folder. current design is a leftover from when every dependency module had its own makefile executed separately
        cleanup workflow with non-default configs
"""

# -----------------------------------------------------------------------------

def concat(strings):
    return ' '.join(filter(None, strings))

class Log:
    Verbose = 0
    Info = 1
    Error = 2

    ErrorColor = '\033[91m'
    ClearColor = '\033[0m'

    def __init__(self):
        self.pad = 0

    def is_enabled(self, level):
        if level == Log.Verbose:
            return BUILD_FLAGS & BUILD_FLAG_VERBOSE
        elif level == Log.Info:
            return True
        elif level == Log.Error:
            return True

    def push(self, level):
        if self.is_enabled(level):
            self.pad = self.pad + 1

    def pop(self, level):
        if self.is_enabled(level):
            self.pad = self.pad - 1

    def print(self, level, str):
        if self.is_enabled(level):
            print('| ' * self.pad + str)

    def info(self, str):
        self.print(Log.Info, str)

    def verbose(self, str):
        self.print(Log.Verbose, str)

    def error(self, str):
        self.print(Log.Error, self.ErrorColor + str + self.ClearColor)

    def push_verbose(self):
        self.push(Log.Verbose)

    def pop_verbose(self):
        self.pop(Log.Verbose)

log = Log()

def output_enum(output_str):
    if output_str == 'exe':
        return OUTPUT_EXE
    elif output_str == 'lib':
        return OUTPUT_LIB
    elif output_str == 'dll':
        return OUTPUT_DLL
    elif output_str == 'inl':
        return OUTPUT_INL
    elif output_str == 'app':
        return OUTPUT_APP
    else:
        return None

def output_str(output_enum):
    if output_enum == OUTPUT_DLL:
        return 'dll'
    elif output_enum == OUTPUT_LIB:
        return 'lib'
    elif output_enum == OUTPUT_EXE:
        return 'exe'
    elif output_enum == OUTPUT_INL:
        return 'inl'
    elif output_enum == OUTPUT_APP:
        return 'app'
    else:
        return None

def config_str(config_enum):
    if config_enum == CONFIG_DEBUG:
        return 'debug'
    elif config_enum == CONFIG_RELEASE:
        return 'release'
    else:
        return None

# Split path into parent (everything but leaf), leaf name (no extension), and leaf extension
def parse_path(path):
    base, name = os.path.split(path)
    name, ext = os.path.splitext(name)
    return base, name, ext

# Normalize path separators
def normpath(path):
    path = os.path.normpath(path)
    path = path.replace('\\', '/')
    return path

def abspath(path):
    return normpath(os.path.abspath(path))

def relpath(path, relative_to):
    try:
        return normpath(os.path.relpath(path, relative_to))
    except:
        # windows can't handle relative paths crossing drives, return the absolute path if that's the case
        return path

def create_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)    

def create_file(path):
    directory, filename = os.path.split(path)
    if not os.path.exists(directory):
        os.makedirs(directory)
    return open(path, 'w')

def string_hash(string):
    h = 5381
    for c in string:
        h = h * 33 ^ ord(c)

    return h

# Simple expression calculator
# only supports integer values
# doesn't support () but follows standard operation order, */ comes before +-
def calc(exp):
    ops = [ '+',           '-',            '*',            '/'           ]
    evs = [lambda a,b:a+b, lambda a,b:a-b, lambda a,b:a*b, lambda a,b:a/b]
    for i, op in enumerate(ops):
        if op in exp:
            left, right = exp.split(op, 1)
            a = int(calc(left))
            b = int(calc(right))
            result = evs[i](a, b)
            return str(result);
    return exp

# Parse .def file into <name, value> list
def parse_def(path):
    log.verbose('Parsing def (' + path + '):')
    log.push_verbose()

    # Read
    file = open(path)
    content = file.read()
    # Add dummy header
    hdr = 'DEFAULT'
    content = '[' + hdr + ']\n' + content
    # Parse
    parser = configparser.RawConfigParser(delimiters=(' '))
    parser.optionxform = lambda option: option
    parser.read_string(content)
    result = []
    for key in parser[hdr]:
        value = parser[hdr][key]
        if value[0].isdigit():
            try:
                value = calc(value)
            except:
                print('[error] Invalid numerical expression: \'' + value + '\' key: \'' + key + '\' file: ' + normpath(os.getcwd() + '/' + path))
                # TODO handle crash better?
                sys.exit()
        else:
            # remove all whitespace, otherwise the compiler will fail to parse the args for cases like /DMACRO = M1 + M2
            value = value.replace(" ", "")
            # check for basic math operators in value and wrap it in () if there's any? the annoying part is that / is also used for paths...
            #if ('+' in value or '-' in value or '*' in value or '/' in value) and '"' not in value:
            #    value = '(' + value + ')'
        log.verbose(key + ' ' + value)
        result.append((key, value))
    file.close()

    log.pop_verbose()
    return result

# Parse manifest bindings file into name -> value map
# manifest bindings example:
#   external_dependency_lib = path_to_lib_file
#   external_dependency_include = path_to_include_folder
def parse_makedef_bindings(path):
    log.verbose('Parsing makedef bindings (' + path + '):')
    log.push_verbose()

    entries = {}
    if os.path.exists(path):
        with open(path) as file:
            for line in file:
                exp = line.split('=')
                name = exp[0].strip()
                value = exp[1].strip()
                if os.path.exists(value):
                    value = normpath(value)
                    #if value[0] != '"':
                    #    value = '"' + value
                    #if value[-1] != '"':
                    #    value = value + '"'
                entries[name] = value

                log.verbose(name + ': ' + str(value))
    else:
        log.verbose('File not found, skip')

    log.pop_verbose()

    return entries

# Parse manifest file into name -> value map
# manifest example:
#   name = fs
#   output = dll
#   configs = debug, release
#   defs = public.def
#   deps = std
#   code = public, private
#   libs = $external_dependency_lib
#   includes = $external_dependency_include
#
def parse_makedef(path, bindings):
    log.verbose('Parsing makedef (' + path + '):')
    log.push_verbose()

    with open(path) as file:
        entries = {}
        skip = False
        for line in file:
            if line.strip().startswith('if'):
                skip = True
                exp = line.strip().split()
                target_platform = exp[1].strip()
                if target_platform == 'win32' and platform.system() == 'Windows':
                    skip = False
                if target_platform == 'linux' and platform.system() == 'Linux':
                    skip = False
                continue

            if line.strip() == 'endif':
                skip = False
                continue

            if skip:
                continue

            exp = line.split('=')
            name = exp[0].strip()
            values = exp[1].strip().split(',')
            values = [x.strip() for x in values]

            if bindings is not None:
                for x in values:
                    if x.startswith('$'):
                        if not x[1:] in bindings:
                            log.error("Missing makedef binding for key " + x + ' while parsing makedef ' + path + '. If not already existing, please create a file named makedef_bindings in the same folder as the makedef, and then add line ' + x + '=' + '...' + ' specifying the desired value for that binding.')

            if bindings is not None:
                values = [x if not x.startswith('$') else bindings[x[1:]] for x in values]
            # TODO misleading: we use "key = value" syntax but we accumulate values instead of overwriting
            entries[name] = entries.get(name, []) + values

            log.verbose(name + ': ' + str(values) )

    log.pop_verbose()

    return entries

# TODO resolve all bindings this way, instead of doing some while parsing the makedef
def resolve_bindings(makedef, bindings):
    entries = {}

    for name in makedef['bindings']:
        if not name in bindings:
            log.error('Module ' + makedef['name'][0] + " is missing makedef binding for key " + name + ' in its makedef. If not already existing, please create a file named makedef_bindings in the same folder as the makedef, and then add line ' + name + '=' + '...' + ' specifying the desired value for that binding.')
        entries[name] = bindings[name]

    return entries

# Accumulates dependencies. modules is read-only and holds the modules to be recursively explored
def gather_dependencies(sln, dependencies, modules):
    for module in modules:
        #module_path = normpath('../' + module)
        #makedef_path = normpath(module_path + '/makedef')
        #content = parse_makedef(makedef_path)
        project = sln.get_project(module)
        for dep in project.module_dependencies:
            if dep not in dependencies:
                dependencies.append(dep)
                gather_dependencies(sln, dependencies, [dep])

# makefile macro
# TODO turn macro lists into python maps and remove this class entirely?
class Macro:
    def __init__(self):
        self.name = ''
        self.value = ''

# makefile target (.o/.lib/... files)
class Target:
    def __init__(self):
        self.name = ''
        self.dependencies = []
        self.cmd = ''
        self.macros = []
        #self.config = ''        # config/release, determines
        #self.modules = []

# Agglomerate of source files that ultimately compiles to one exe/lib/dll
class Project:
    def __init__(self, index, name):
        self.index = index
        self.name = name
        self.path = normpath(index.workspace_map[name])
        self.solution = None
        self.output = OUTPUT_EXE
        self.project_paths = []
        self.project_ignores = []
        self.external_paths = []
        self.external_libs = []
        self.external_dlls = []
        self.external_exes = []
        self.defines = []
        self.module_dependencies = []
        self.external_depencencies = []
        self.bindings = {}
        self.makedef_path = None
        self.makedef_bindings_path = None

        self.launcher_path = None # only used when output == OUTPUT_APP

        self.inc = []
        self.src = []

        self.targets = []
        self.main_targets = []

        self.config = None # TODO remove and only reference parent Solution config

    # Search for and map header and source files
    # Looks in project_paths and external_paths and respects project_ignores (applies to both project and external)
    def map_files(self):
        log.verbose('Mapping source files:')
        log.push_verbose()

        self.inc = []
        self.src = []
        for absolute_path in self.project_paths + self.external_paths:
            #absolute_path = normpath(self.path + '/' + relative_path)
            if os.path.isdir(absolute_path):
                for filename in os.listdir(absolute_path):
                    skip = False
                    for ignore in self.project_ignores:
                        if filename.startswith(ignore):
                            skip = True
                            break
                    filepath = normpath(absolute_path + '/' + filename)
                    if skip:
                        log.verbose('SKIP: ' + filepath)
                        continue
                    if filename.endswith(".h") or filename.endswith(".inl"):
                        log.verbose('INC: ' + filepath)
                        self.inc.append(filepath)
                    elif filename.endswith(".c"):
                        log.verbose('SRC: ' + filepath)
                        self.src.append(filepath)
        log.pop_verbose()

    # Resolve module dependencies by converting them to 'normal' dependencies
    def gather_module_dependencies(self):
        log.verbose("Gathering module dependencies...")
        log.push_verbose()

        gather_dependencies(self.solution, self.module_dependencies, self.module_dependencies)

        for module in self.module_dependencies:
            #module_path = normpath(self.path + '/../' + module)
            #makedef_path = normpath(module_path + '/makedef')
            #content = parse_makedef(makedef_path)
            project = self.solution.get_project(module)
            output = project.output
            defs = project.defines

            module_path = self.index.workspace_map[module]

            if output != OUTPUT_INL:
                output_container = None
                if output == OUTPUT_DLL:
                    output_container = self.external_dlls
                elif output == OUTPUT_LIB:
                    output_container = self.external_libs
                elif output == OUTPUT_EXE:
                    output_container = self.external_exes

                config = config_str(self.config)
                path = normpath(module_path + '/build/' + config + '/output/' + module + '.' + output_str(output))
                #log.verbose("EXT DEP: " + path)
                output_container.append(path)
                self.external_depencencies.append(path)

            if output == OUTPUT_LIB:
                for lib in project.external_libs:
                    #log.verbose("EXT LIB: " + lib)
                    self.external_libs.append(lib)

            #for define in defs:
            #    path = normpath(define)
            #    self.defines.append(path)

            include_path = normpath(module_path + '/public')
            #log.verbose("EXT INC: " + include_path)
            self.external_paths.append(include_path)

            include_defs = normpath(include_path + '/define')
            if (os.path.exists(include_defs)):
                self.defines.append(include_defs)

        if self.output == OUTPUT_APP:
            # todo add to external_exes?
            config_name = config_str(self.config)
            launcher_name = 'std_launcher'
            launcher_path = self.index.workspace_map[launcher_name]
            self.launcher_path = normpath(launcher_path + '/build/' + config_name + '/output/' + 'std_launcher.exe')
            #log.verbose('EXT EXE: ' + launcher_name)

        log.pop_verbose()

    # Normalizes all stored paths and looks for the correspondent files
    def prepare(self, rootpath):
        self.gather_module_dependencies()

        self.project_paths = [normpath(self.path + '/' + path) for path in self.project_paths]
        self.project_ignores = [normpath(path) for path in self.project_ignores]
        self.external_paths = [normpath(path) for path in self.external_paths]

        log.verbose('Project paths:')
        log.push_verbose()
        for path in self.project_paths:
            log.verbose(path)
        log.pop_verbose()

        log.verbose('Project ignores:')
        log.push_verbose()
        for ignore in self.project_ignores:
            log.verbose(ignore)
        log.pop_verbose()

        log.verbose('External include paths:')
        log.push_verbose()
        for path in self.external_paths:
            log.verbose(path)
        log.pop_verbose()

        log.verbose('Submodule dependencies:')
        log.push_verbose()
        for dep in self.external_depencencies:
            log.verbose(dep)
        log.pop_verbose()

        # TODO what are these 2 blocks for?
        log.verbose("External libs:")
        log.push_verbose()
        external_libs = []
        for lib in self.external_libs:
            lib = normpath(lib)
            log.verbose("EXT LIB: " + lib)
            external_libs.append(lib)
        self.external_libs = external_libs
        log.pop_verbose()

        log.verbose("External dlls:")
        log.push_verbose()
        external_dlls = []
        for dll in self.external_dlls:
            dll = normpath(dll)
            log.verbose("EXT DLL: " + dll)
            external_dlls.append(dll)
        self.external_dlls = external_dlls
        log.pop_verbose()
        # TODO see above

        self.map_files()
        return True

    # Builds the make targets
    def build(self, rootpath, build_path, solution_name):
        # call prepare (normalize paths and map files) first
        if (not self.prepare(self.path)):
            return

        # Group all header files dependencies into a makefile macro.
        # Each source file gets to depend on all headers, regardless of its includes.
        headers_macro = Macro()
        headers_macro.name = self.name.upper() + '_HEADERS'
        headers_macro.value = ''
        log.verbose('Building project headers list (' + headers_macro.name + '):')
        log.push_verbose()
        for header in self.inc:
            path = relpath(header, rootpath + '/' + build_path)
            headers_macro.value += path.replace(" ", "\\ ") + ' ' # Escape spaces in paths, can't wrap macro paths with " "
            log.verbose(path)
        # Add in here headers coming from dependencies too
        for module in self.module_dependencies:
            if module != 'std':
                headers_path = normpath('../' + module + '/public/')
                wildcard = '$(wildcard ' + headers_path + '/*)'
                log.verbose(wildcard)
                headers_macro.value += wildcard + ' ' #headers_path + module + '.h '
        log.pop_verbose()

        # Group all .def files
        defs_macro = Macro()
        defs_macro.name = self.name.upper() + '_DEFS'
        defs_macro.value = ''
        log.verbose('Building project .def files list (' + defs_macro.name + '):')
        log.push_verbose()
        for define in self.defines:
            path = relpath(define, rootpath + '/' + build_path)
            log.verbose(path)
            defs_macro.value += path + ' '
        log.verbose(self.makedef_path)
        defs_macro.value += self.makedef_path + ' '
        if (self.makedef_bindings_path is not None):
            log.verbose(self.makedef_bindings_path)
            defs_macro.value += self.makedef_bindings_path + ' '
        log.pop_verbose()

        # Targets are generated for each config. Dependencies are the same but paths and compile flags differ.
        targets = []
        config_flags = ''
        # TODO FIX THIS, THIS DOESN'T WORK FOR RELEASE
        # Process currently iterated config and prepare tokens to use later
        config_path = config_str(self.config)
        if self.config == CONFIG_DEBUG:
            config_flags = '-O0'
        elif self.config == CONFIG_RELEASE:
            config_flags = '-O3'
        else:
            assert False

        output_path = relpath(self.path + '/build/' + config_path + '/output', rootpath + '/' + build_path)

        # Group all objs that will be created by a compile step on source files (/src) into a macro.
        # example: <project>OBJS<config> = x.o y.o ...
        objs_macro = Macro()
        objs_macro.name = self.name.upper() + '_OBJS'
        log.verbose('Building project obj list (' + objs_macro.name + ') for config ' + config_path)
        log.push_verbose()
        for src in self.src:
            _, name, _ = parse_path(src)
            path = output_path + '/' + name + '.o'
            objs_macro.value += path + ' '
            log.verbose(path)
        log.pop_verbose()

        # Generate the main target for this config, which will produce the lib/exe. It depends on all objs.
        # output/<config>/<project>.<lib/exe>: <objs>
        main_target = Target()
        self.main_targets.append(main_target)
        #main_target.config = config_path
        #main_target.modules = self.module_dependencies
        main_target.macros.append(objs_macro)
        objs_dep = '$(' + objs_macro.name + ')'
        main_target.dependencies = [objs_dep]
        for dep in self.external_depencencies:
            main_target.dependencies.append(' ' + relpath(dep, rootpath + '/' + build_path))

        def_cmd = '-Dstd_module_name_m=' + self.name
        def_cmd += ' -Dstd_module_path_m=' + '\\"' + normpath(self.index.workspace_map[self.name]) + '/\\"'
        def_cmd += ' -Dstd_solution_module_name_m=' + solution_name
        def_cmd += ' -Dstd_builder_path_m=\\"' + normpath(self.index.tool_path) + '/cli.py\\"'
        if self.config == CONFIG_DEBUG:
            def_cmd += ' -Dstd_build_debug_m=1'
            def_cmd += ' -Dstd_submodules_path_m=\\"submodules/debug/\\"'
        elif self.config == CONFIG_RELEASE:
            def_cmd += ' -Dstd_build_debug_m=0'
            def_cmd += ' -Dstd_submodules_path_m=\\"submodules/release/\\"'
        def_cmd += ' -Dstd_rootpath_m=\\"' + normpath(self.index.root_path) + '/\\"'
        stack_size = '1000000'
        # Parse all def files
        for path in self.defines:
            defs = parse_def(normpath(path))
            for d in defs:
                key = d[0]
                value = d[1]
                if key == 'std_thread_stack_size_m':
                    stack_size = value
                if (value[0] == '"' and value[-1] == '"') or (value[0] == '\'' and value[-1] == '\''):
                    value = '\\"' + value[1:-1] + '\\"'
                def_cmd += ' -D' + key + '=' + value

        for key in self.bindings:
            value = self.bindings[key]
            if value.isnumeric():
                def_cmd += ' -Dstd_binding_' + key + '_m=' + value
            else:
                def_cmd += ' -Dstd_binding_' + key + '_m=' + '"\\"' + value + '\\""'

        compiler_defs_file = create_file(relpath(self.path + '/build/' + config_path, rootpath) + '/defines')
        compiler_defs_file.write(def_cmd)

        compiler = COMPILER_CLANG

        if compiler == COMPILER_CLANG:
            std = 'c99'
        elif compiler == COMPILER_GCC:
            std = 'gnu23'

        main_target.cmd = ''
        if compiler == COMPILER_CLANG:
            if platform.system() == 'Windows':
                if self.output == OUTPUT_LIB:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.lib')
                    main_target.cmd += '\t@llvm-ar crus $@ ' + objs_dep
                elif self.output == OUTPUT_DLL or self.output == OUTPUT_APP:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.dll')
                    #main_target.cmd += '\t@clang-cl -Fa -Z7 -LD -o $@ ' + objs_dep
                    main_target.cmd += '\t@clang -std=' + std + ' -g -shared ' + config_flags + ' -o $@ ' + objs_dep
                elif self.output == OUTPUT_EXE:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.exe')
                    #main_target.cmd += '\t@clang-cl -Fa -Z7 /entry:mainCRTStartup -o $@ ' + objs_dep
                    main_target.cmd += '\t@clang -std=' + std + ' -g ' + config_flags + ' -o $@ ' + objs_dep + ' -Wl,/STACK:' + stack_size
            elif platform.system() == 'Linux':
                # -rdynamic preserves code symbols, allows to print callstack
                if self.output == OUTPUT_LIB:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.lib')
                    # removed u flag from win32 version. https://bugzilla.redhat.com/show_bug.cgi?id=1155273
                    main_target.cmd += '\t@ar crs $@ ' + objs_dep
                elif self.output == OUTPUT_DLL or self.output == OUTPUT_APP:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.dll')
                    #main_target.cmd += '\t@clang-cl -Fa -Z7 -LD -o $@ ' + objs_dep
                    main_target.cmd += '\t@clang -rdynamic -shared ' + config_flags + ' -o $@ ' + objs_dep
                elif self.output == OUTPUT_EXE:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.exe')
                    # -lX11 -lvulkan -lpthread -lm -ldl
                    main_target.cmd += '\t@clang -rdynamic ' + config_flags + ' -o $@ ' + objs_dep + ' -Wl,-zstack-size=' + stack_size
        elif compiler == COMPILER_GCC:
            if platform.system() == 'Windows':
                if self.output == OUTPUT_LIB:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.lib')
                    main_target.cmd += '\t@ar crus $@ ' + objs_dep
                elif self.output == OUTPUT_DLL or self.output == OUTPUT_APP:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.dll')
                    main_target.cmd += '\t@gcc -std=' + std + ' -g -shared ' + config_flags + ' -o $@ ' + objs_dep
                elif self.output == OUTPUT_EXE:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.exe')
                    main_target.cmd += '\t@gcc -std=' + std + ' -g ' + config_flags + ' -o $@ ' + objs_dep + ' -Wl,-stack,' + stack_size
            elif platform.system() == 'Linux':
                if self.output == OUTPUT_LIB:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.lib')
                    # removed u flag from win32 version. https://bugzilla.redhat.com/show_bug.cgi?id=1155273
                    main_target.cmd += '\t@ar crs $@ ' + objs_dep
                elif self.output == OUTPUT_DLL or self.output == OUTPUT_APP:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.dll')
                    main_target.cmd += '\t@gcc -shared ' + config_flags + ' -o $@ ' + objs_dep
                elif self.output == OUTPUT_EXE:
                    main_target.name = normpath(output_path + '/' + self.name.lower() + '.exe')
                    main_target.cmd += '\t@gcc ' + config_flags + ' -o $@ ' + objs_dep + ' -Wl,-zstack-size=' + stack_size

        # When target is LIB, ignore the other .lib dependency. It will get linked in by the final target (DLL or EXE) that also includes this lib
        if self.output == OUTPUT_DLL or self.output == OUTPUT_EXE or self.output == OUTPUT_APP: # or self.output == OUTPUT_LIB:
            if platform.system() == 'Windows':
                for lib in self.external_libs:
                    if compiler == COMPILER_CLANG:
                        main_target.cmd += ' -l' + '"' + lib + '"'
                    elif compiler == COMPILER_GCC:
                        base, name, ext = parse_path(lib)
                        if base == '':
                            main_target.cmd += ' -l' + '' + name + ''
                        else:
                            main_target.cmd += ' -L' + '' + base + '' + ' -l' + '' + name + ''
            elif platform.system() == 'Linux': # TODO not sure if this is a win32/linux or clang/gcc thing... ?
                for lib in self.external_libs:
                    base, name, ext = parse_path(lib)
                    if ext == '':
                        main_target.cmd += ' -l' + lib
                    else:
                        main_target.cmd += ' ' + lib

        if (BUILD_FLAGS & BUILD_FLAG_OUTPUT_PP) or (BUILD_FLAGS & BUILD_FLAG_OUTPUT_ASM):
            main_target.cmd = ''

        targets.append(main_target)

        # Generate the DLLs only target, used when live-reloading modules
        # Group all dll module dependencies
        if self.name == solution_name:
            dlls_macro = Macro()
            dlls_macro.name = self.name.upper() + '_DLLS'
            for dep in self.external_depencencies:
                _, _, ext = parse_path(dep)
                if ext == '.dll':
                    dlls_macro.value += relpath(dep, rootpath + '/' + build_path) + ' '

            dlls_target = Target()
            dlls_target.macros.append(dlls_macro)
            dlls_dep = '$(' + dlls_macro.name + ')'
            dlls_target.dependencies.append(dlls_dep)
            targets.append(dlls_target)

        # Prepare obj compilation flags
        platform_flags = ''
        if platform.system() == 'Windows':
            #platform_flags = '/J' # default char to unsigned
            platform_flags = '-funsigned-char' # default char to unsigned
            platform_flags += ' -DWINVER=' + WINNT_VERSION + ' -D_WIN32_WINNT=' + WINNT_VERSION # TODO is there a way to avoid having to do this?
        elif platform.system() == 'Linux':
            platform_flags = '-funsigned-char' # default char to unsigned

        compile_flags = concat(['-Wall', platform_flags, config_flags, CORE_WARNINGS_FLAGS])
        #compile_flags = '-Wall' + ' ' + platform_flags + ' ' + config_flags + ' ' + CORE_WARNINGS_FLAGS
        if not BUILD_FLAGS & BUILD_FLAG_PERMISSIVE_WARNINGS:
            #compile_flags += ' ' + EXTENDED_WARNINGS_FLAGS
            compile_flags = concat([compile_flags, EXTENDED_WARNINGS_FLAGS])

        compiler_flags_file = create_file(relpath(self.path + '/build/' + config_path, rootpath) + '/flags')
        compiler_flags_file.write(compile_flags)

        # Generates one target for each obj, each depending on the corresponding source file and all header files.
        # output/<config>/<src>.o: src/<src>.c <headers>
        for src in self.src:
            target = Target()
            _, name, ext = parse_path(src)
            target.name = normpath(output_path + '/' + name + '.o')
            target.dependencies = [relpath(src, rootpath + '/' + build_path), '$(' + headers_macro.name + ')', '$(' + defs_macro.name + ')']

            src_def_cmd = '-Dstd_file_name_m=' + name + ext
            src_def_cmd += ' -Dstd_file_name_hash_m=' + str(string_hash(name))

            if compiler == COMPILER_CLANG:
                if platform.system() == 'Windows':
                    # TODO make asm output optional?
                    #target.cmd = '\t@clang-cl ' + config_flags + ' -Zi -Fa' + normpath(output_path + '/' + name + '.asm') + ' '
                    target.cmd = '\t@clang -std=' + std + ' -g -gcodeview --target=x86_64-windows-msvc'# -Fa' + normpath(output_path + '/' + name + '.asm')
                    for path in self.project_paths:
                        #target.cmd += ' -I' + relpath(self.path + '/' + path, rootpath)
                        target.cmd += ' -I' + '"' + relpath(path, rootpath + '/' + build_path) + '"'
                    for path in self.external_paths:
                        # TODO for each external path create a macro instead of referencing its path directly?
                        #      that way it would be easier to configure external references when sharing the generated makefile
                        #target.cmd += ' -I' + relpath(self.path + '/' + path, rootpath)
                        target.cmd += ' -I' + '"' + relpath(path, rootpath + '/' + build_path) + '"'
                    #target.cmd += ' -Wall /J ' + CORE_WARNINGS_FLAGS
                    #if not BUILD_FLAGS & BUILD_FLAG_PERMISSIVE_WARNINGS:
                    #    target.cmd += ' ' + EXTENDED_WARNINGS_FLAGS
                    target.cmd += ' -o $@ -c ' + relpath(src, rootpath + '/' + build_path)
                    #target.cmd += ' -DWINVER=' + WINNT_VERSION + ' -D_WIN32_WINNT=' + WINNT_VERSION
                    target.cmd += ' ' + src_def_cmd
                    #target.cmd += ' ' + def_cmd
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/defines')
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/flags')
                elif platform.system() == 'Linux':
                    target.cmd = '\t@clang ' + config_flags
                    if BUILD_FLAGS & BUILD_FLAG_OUTPUT_ASM:
                        target.cmd += ' -S'
                        target.cmd += ' -mllvm --x86-asm-syntax=intel'
                    for path in self.project_paths:
                        target.cmd += ' -I' + path
                    for path in self.external_paths:
                        target.cmd += ' -I' + path
                    target.cmd += ' -Wall -funsigned-char ' + CORE_WARNINGS_FLAGS
                    if not BUILD_FLAGS & BUILD_FLAG_PERMISSIVE_WARNINGS:
                        target.cmd += ' ' + EXTENDED_WARNINGS_FLAGS
                    target.cmd += ' -g -c ' + relpath(src, rootpath + '/' + build_path)
                    if BUILD_FLAGS & BUILD_FLAG_OUTPUT_ASM:
                        target.cmd += ' -o ' + normpath(output_path + '/' + name + '.asm')
                    else:
                        target.cmd += ' -o $@'
                    target.cmd += ' -fPIC' # needed when building a shared lib, static libs used by the shared lib must also have this...
                    target.cmd += ' ' + src_def_cmd
                    #target.cmd += ' ' + def_cmd
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/defines')
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/flags')
            elif compiler == COMPILER_GCC:
                if platform.system() == 'Windows':
                    # TODO make asm output optional?
                    target.cmd = '\t@gcc -std=' + std + ' -gcodeview'# --target=x86_64-windows-msvc'# -Fa' + normpath(output_path + '/' + name + '.asm')
                    for path in self.project_paths:
                        target.cmd += ' -I' + '"' + relpath(path, rootpath + '/' + build_path) + '"'
                    for path in self.external_paths:
                        # TODO for each external path create a macro instead of referencing its path directly?
                        #      that way it would be easier to configure external references when sharing the generated makefile
                        #target.cmd += ' -I' + relpath(self.path + '/' + path, rootpath)
                        target.cmd += ' -I' + '"' + relpath(path, rootpath + '/' + build_path) + '"'
                    #target.cmd += ' -Wall /J ' + CORE_WARNINGS_FLAGS
                    #if not BUILD_FLAGS & BUILD_FLAG_PERMISSIVE_WARNINGS:
                    #    target.cmd += ' ' + EXTENDED_WARNINGS_FLAGS
                    target.cmd += ' -o $@ -c ' + relpath(src, rootpath + '/' + build_path)
                    #target.cmd += ' -DWINVER=' + WINNT_VERSION + ' -D_WIN32_WINNT=' + WINNT_VERSION
                    target.cmd += ' ' + src_def_cmd
                    #target.cmd += ' ' + def_cmd
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/defines')
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/flags')
                elif platform.system() == 'Linux':
                    target.cmd = '\t@gcc -std=' + std + ' ' + config_flags
                    if BUILD_FLAGS & BUILD_FLAG_OUTPUT_ASM:
                        target.cmd += ' -S'
                        target.cmd += ' -mllvm --x86-asm-syntax=intel'
                    for path in self.project_paths:
                        target.cmd += ' -I' + path
                    for path in self.external_paths:
                        target.cmd += ' -I' + path
                    target.cmd += ' -Wall -funsigned-char ' + CORE_WARNINGS_FLAGS
                    if not BUILD_FLAGS & BUILD_FLAG_PERMISSIVE_WARNINGS:
                        target.cmd += ' ' + EXTENDED_WARNINGS_FLAGS
                    target.cmd += ' -g -c ' + relpath(src, rootpath + '/' + build_path)
                    if BUILD_FLAGS & BUILD_FLAG_OUTPUT_ASM:
                        target.cmd += ' -o ' + normpath(output_path + '/' + name + '.asm')
                    else:
                        target.cmd += ' -o $@'
                    target.cmd += ' -fPIC' # needed when building a shared lib, static libs used by the shared lib must also have this...
                    target.cmd += ' ' + src_def_cmd
                    #target.cmd += ' ' + def_cmd
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/defines')
                    target.cmd += ' @' + normpath(relpath(self.path + '/build/' + config_path, rootpath + '/' + build_path) + '/flags')

            #target.cmd += ' -march=native'

            #TODO test this, is it working?
            if (BUILD_FLAGS & BUILD_FLAG_OUTPUT_PP):
                #if platform.system() == 'Linux':
                if compiler == COMPILER_CLANG:
                    target.cmd = '\t@clang -E ' + relpath(src, rootpath + '/' + build_path) + ' > ' + normpath(output_path + '/' + name + '.pp')
                elif compiler == COMPILER_GCC:
                    target.cmd = '\t@gcc -E ' + relpath(src, rootpath + '/' + build_path) + ' > ' + normpath(output_path + '/' + name + '.pp')

                for path in self.project_paths:
                    target.cmd += ' -I' + path
                for path in self.external_paths:
                    target.cmd += ' -I' + path
                #target.cmd += ' ' + src_def_cmd
                #target.cmd += ' ' + def_cmd

            target.macros.append(headers_macro)
            target.macros.append(defs_macro)
            targets.append(target)

        self.targets = targets

        create_dir(relpath(self.path + '/build/' + config_path, rootpath) + '/output/')

    # external_dlls are located and copied locally as part of the build process by this script
    def gather_dlls(self, path):
        copied_dlls = []

        config = config_str(self.config)
        modules_path = normpath(path + '/' + config)

        # TODO remove this, use external_exes instead
        if self.output == OUTPUT_APP and not (BUILD_FLAGS & BUILD_FLAG_RELOAD):            
            create_dir(modules_path)
            base, name, ext = parse_path(self.launcher_path)
            shutil.copy(self.launcher_path, modules_path)
            output_path = self.path + '/build/' + config + '/output'
            dll = output_path + '/' + self.name + '.dll'
            shutil.copy(dll, modules_path)

        dlls_to_copy = self.external_dlls.copy()
        if self.output == OUTPUT_APP:
            output_path = self.path + '/build/' + config + '/output'
            dll = output_path + '/' + self.name + '.dll'
            dlls_to_copy.append((config, dll))

        if len(dlls_to_copy) > 0:
            create_dir(modules_path)

        for item in dlls_to_copy:
            config = ''
            dll = ''
            if type(item) in [list, tuple]:
                config = item[0] + '/'
                dll = item[1]
            else:
                dll = item
            base, name, ext = parse_path(dll)

            dest_dll_path = normpath(modules_path + '/' + name + ext)
            source_dll_path = dll
            dest_time = 0
            source_time = 1
            if os.path.exists(dest_dll_path):
                dest_time = os.path.getmtime(dest_dll_path)
            if os.path.exists(source_dll_path):
                source_time = os.path.getmtime(source_dll_path)
            if source_time > dest_time:
                copied_dlls.append(name)

                alias_dll_path = normpath(modules_path + '/' + '___' + name + ext)

                # print('\t' + source_dll_path)
                # this will delete the .dll even if there's a process still running that's loaded it in the past
                # needed for live reload
                
                #log.verbose('Deleting ' + normpath(modules_path + '/' + name + ext))
                #os.system('rm -f ' + normpath(modules_path + '/' + name + ext))
                
                if (os.path.exists(alias_dll_path)):
                    log.verbose('Deleting ' + alias_dll_path)
                    os.system('rm -f ' + alias_dll_path)

                if (os.path.exists(dest_dll_path)):
                    log.verbose('Renaming ' + dest_dll_path + ' to ' + alias_dll_path)
                    #log.verbose('mv ' + normpath(modules_path + '/' + name + ext) + ' ' + normpath(modules_path + '/' + '___' + name + ext))
                    # TODO make silent to avoid warning
                    os.system('mv ' + dest_dll_path + ' ' + alias_dll_path)

                log.verbose('Copying ' + dll + ' to ' + dest_dll_path)
                shutil.copy(dll, dest_dll_path)
                if platform.system() == 'Windows':
                    log.verbose('Deleting ' + normpath(modules_path + '/' + name + '.pdb'))
                    os.system('rm -f ' + normpath(modules_path + '/' + name + '.pdb'))
                    log.verbose('Copying ' + base + '/' + name + '.pdb' + ' to ' + normpath(modules_path + '/' + name + '.pdb'))
                    shutil.copy(base + '/' + name + '.pdb', normpath(modules_path + '/' + name + '.pdb'))
        return copied_dlls

# A solution contains the main module and all its dependencies as projects.
class Solution:
    def __init__(self, root, name):
        self.root = root
        self.name = name
        self.projects = {}
        self.custom_targets = DEFAULT_CUSTOM_TARGETS
        self.defines = []
        self.main_project = None

        if BUILD_FLAGS & BUILD_FLAG_OPTIMIZATION_OFF:
            self.config = CONFIG_DEBUG
        else:
            self.config = CONFIG_RELEASE

    def add_project(self, project):
        self.projects[project.name] = project
        project.solution = self
        project.config = self.config
        self.main_project = project

    def get_project(self, name):
        if name not in self.projects:
            return None
        return self.projects[name]

    def gather_dlls(self):
        return self.main_project.gather_dlls(normpath(self.main_project.path + '/submodules'))

    def output_build_changes(self, changelist):
        message = str(len(changelist)) + '\0'
        for module in changelist:
            message = message + module
            message = message + '\0'
        pipe = win32file.CreateFile('\\\\.\\pipe\\' + BUILD_CHANGES_OUTPUT_PIPE_NAME, win32file.GENERIC_WRITE, 0, None, win32file.OPEN_EXISTING, 0, None)
        #win32pipe.SetNamedPipeHandleState(pipe, win32pipe.PIPE_READMODE_BYTE, None, None)
        win32file.WriteFile(pipe, message.encode(), None)
        pipe.close()

    def output_build_error(self):
        message = '-1' + '\0'
        pipe = win32file.CreateFile('\\\\.\\pipe\\' + BUILD_CHANGES_OUTPUT_PIPE_NAME, win32file.GENERIC_WRITE, 0, None, win32file.OPEN_EXISTING, 0, None)
        win32file.WriteFile(pipe, message.encode(), None)
        pipe.close()

    def get_build_path(self):
        config_path = config_str(self.config)
        makefile_path = '/build/' + config_path
        return makefile_path

    # Writes the make file
    def write(self):
        # Create makefile
        config_path = config_str(self.config)
        makefile_path = normpath(self.main_project.path + '/build/' + config_path + '/Makefile')
        makefile = create_file(makefile_path)
        targets_map = {}
        for key in self.projects:
            project = self.projects[key]
            #project.build(path)
            log.verbose('Building project ' + project.name + ' (' + project.path + ')')
            log.push_verbose()
            project.build(self.main_project.path, 'build/' + config_path, self.name)
            log.pop_verbose()
            targets_map[project] = project.targets
            #if project.output == OUTPUT_EXE:
            #    project.gather_dlls(path + '/modules')
        # Build makefile macros lookup map
        macros = {}
        for _, project in self.projects.items():
            for target in project.targets:
                for macro in target.macros:
                    if macro.name in macros.keys():
                        lookup = macros[macro.name]
                        if lookup != macro:
                            log.error("Found duplicate macro while generating makefile")
                    macros[macro.name] = macro
        # Write the makefile
        log.verbose('Writing makefile (' + makefile_path + ')')
        phony = []
        # --- ALL target ---
        if TARGET_ALL in self.custom_targets:
            makefile.write('all:')
            makefile.write(' ' + self.main_project.main_targets[0].name)
            makefile.write('\n\n')
            phony.append('all')
        # --- CLEAN target ---
        if TARGET_CLEAN in self.custom_targets:
            makefile.write('clean:\n\t')
            separator = ''
            path = relpath(self.main_project.path + '/build/' + config_path + '/output/*', self.root)
            makefile.write(separator + 'rm -rf ' + path)
            separator = ' && '
            path = relpath(self.main_project.path + '/submodules/' + config_path + '/*', self.root)
            makefile.write(separator + 'rm -rf ' + path)
            makefile.write('\n\n')
            phony.append('clean')
        # --- Dump the projects ---
        for _, project in self.projects.items():
            # -- Write the project macros ---
            for target in project.targets:
                for macro in target.macros:
                    if macro.name in macros.keys():
                        lookup = macros[macro.name]
                        del macros[macro.name]
                        makefile.write(lookup.name + '=' + lookup.value + '\n\n')
            # -- Write the project targets ---
            for target in project.targets:
                if target.name != '':
                    makefile.write(target.name)
                    if target.dependencies:
                        makefile.write(':')
                        for dep in target.dependencies:
                            makefile.write(' ' + dep)
                    #makefile.write('\n\t@printf "\\t$@\\n"\n')
                    makefile.write('\n\t@printf "\\t$(notdir $@)\\n"\n')
                    if target in project.main_targets:
                        _, pdb_name, _ = parse_path(target.name)
                        pdb_path = relpath(project.path + '/build/' + config_path + '/output/' + pdb_name + '.pdb', self.root)
                        # TODO this forces a full pdb rebuild, might be slowing things down
                        # why is this needed? why is the main exe sometime locking the pdb (e.g. std_launcher for viewer_app)?
                        makefile.write('\t@rm -f ' + abspath(pdb_path) + '\n')
                    makefile.write(target.cmd)
                    makefile.write('\n\n')
        # --- RELOAD target ---
        makefile.write('reload:')
        if self.main_project.output == OUTPUT_APP:
            makefile.write(' ' + '$(' + self.name.upper() + '_DLLS' + ') ' + self.main_project.main_targets[0].name)
        else:
            makefile.write(' ' + '$(' + self.name.upper() + '_DLLS' + ')')
        makefile.write('\n\n')
        # --- PHONY target ---
        makefile.write('.PHONY: ')
        for target in phony:
            makefile.write(target + ' ')
        # Done.
        makefile.close()

# Creates a solution containing the module at specified path and all its dependencies. Can be used to generate the makefile to build the module.
# Intended usage: instantiate the generator by passing in the workspace path, call load, call generate, call gather_dlls to move all generated dependency dlls into the main workspace deps folder
class Generator:
    def __init__(self, index, main_project, build_flags):
        global BUILD_FLAGS
        self.index = index
        self.main_project = main_project
        BUILD_FLAGS = build_flags
        self.projects = []

    def __has_project(self, name):
        for project in self.projects:
            if project.name == name:
                return True
        return False

    # TODO scan all workspace for makedef files first instead of assuming they can be looked up recursively in /../dependency/
    def __load_node_rec(self, path):
        # Parse makedef and fill the project with the makedef data and the solution with the project
        makedef_path = abspath(path + '/makedef')
        makedef_bindings_path = abspath(path + '/makedef_bindings')
        makedef_bindings = parse_makedef_bindings(makedef_bindings_path)
        makedef = parse_makedef(makedef_path, makedef_bindings)

        name = makedef['name'][0]

        output = output_enum(makedef['output'][0])

        project = Project(self.index, name)
        project.output = output
        project.project_paths = makedef['code']
        for project_path in project.project_paths:
            def_path = normpath(path + '/' + project_path + '/define')
            if (os.path.exists(def_path)):
                project.defines.append(def_path)
        project.makedef_path = makedef_path
        if (os.path.exists(makedef_bindings_path)):
            project.makedef_bindings_path = makedef_bindings_path
        #if 'defs' in makedef:
        #    for define in makedef['defs']:
        #        project.defines.append(path + '/' + define)
        if 'deps' in makedef:
            project.module_dependencies = makedef['deps']
        if 'ignorelist' in makedef:
            project.project_ignores = makedef['ignorelist']
        elif 'ignores' in makedef:
            project.project_ignores = makedef['ignores']
        if 'includes' in makedef:
            project.external_paths = makedef['includes']
        if 'libs' in makedef:
            project.external_libs = makedef['libs']
        if 'dlls' in makedef:
            project.external_dlls = makedef['dlls']
        if 'bindings' in makedef:
            project.bindings = resolve_bindings(makedef, makedef_bindings)

        if project.output == OUTPUT_APP:
            project.module_dependencies = project.module_dependencies + ['std_launcher']
            log.verbose('Adding std_launcher dependency')

        self.projects.append(project)
        log.verbose("Found project " + project.name + " at root " + project.path)

        # Recursive load all dependencies
        for dep in project.module_dependencies:
            if not self.__has_project(dep):
                #path = os.getcwd();
                node_path = self.index.workspace_map[dep]
                #os.chdir(node_path)
                self.__load_node_rec(node_path)
                #os.chdir(path)
        return name

    def load(self):
        log.verbose('Loading makedef graph...')
        log.push_verbose()

        solution_path = self.index.workspace_map[self.main_project]

        name = self.__load_node_rec(solution_path)
        self.projects.reverse()

        log.verbose('Final dependency list:')
        log.push_verbose()
        self.solution = Solution(solution_path, name)
        for project in self.projects:
            log.verbose(project.name)
            self.solution.add_project(project)
        log.pop_verbose()

        log.pop_verbose()

    def generate(self):
        log.info('Generating makefiles...')
        log.push_verbose()
        self.solution.write()
        log.pop_verbose()

    def gather_dlls(self):
        log.info('Gathering dlls...')
        log.push_verbose()
        changelist = self.solution.gather_dlls()
        log.pop_verbose()
        return changelist

    # These go through the output pipe. For reloads only
    def output_build_changes(self, changelist):
        log.info('Writing out build changes...')
        log.push_verbose()
        self.solution.output_build_changes(changelist)
        log.pop_verbose()

    def output_build_error(self):
        log.info('Writing out build error...')
        log.push_verbose()
        self.solution.output_build_error()
        log.pop_verbose()

    def get_build_path(self):
        return self.solution.get_build_path()

class Index:
    def init(self, root_path, tool_path, workspace_paths):
        # full paths for root/tool/workspaces
        self.root_path = root_path
        self.tool_path = tool_path
        self.workspace_paths = workspace_paths
    
        # tool path name (last token in the tool path, name of the final folder)
        self.tool_name = os.path.basename(os.path.normpath(tool_path))
        # maps workspaces paths names (last token in each workspace path) to their full path
        self.workspace_names = dict( ( os.path.basename(os.path.normpath(x)), x ) for x in workspace_paths) # workspace name -> workspace path
        
        # maps workspace paths to lists of actual workspace (subfolders) names contained in that path
        self.workspace_lists = {}
        # maps actual workspace names (see above) to their full path
        self.workspace_map = {}

        for workspace in self.workspace_paths:
            path = os.path.join(root_path, workspace)
            items = os.listdir(path)
            for item in items:
                item_path = os.path.join(path, item)
                if os.path.isdir(item_path):
                    self.workspace_map[item] = os.path.abspath(item_path)
                    if workspace not in self.workspace_lists:
                        self.workspace_lists[workspace] = []
                    self.workspace_lists[workspace] = self.workspace_lists[workspace] + [item]
