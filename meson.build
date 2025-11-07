project('luna','c',
        default_options:['c_std=c2x']
)

includedir=include_directories('include')

ds_srcs = ['src/stb_ds.c','src/arena.c','src/strbuf.c','src/worklist.c']
parser_srcs=['src/parser/tokenizer.c','src/parser/parser.c','src/parser/ast.c']

runtime_srcs=['src/heap.c','src/value.c','src/string_interner.c','src/bytecode/generator.c',
            'src/bytecode/interpreter.c','src/bytecode/vm.c','src/bytecode/ir.c',
            'src/objects/global.c','src/objects/object_prototype.c','src/objects/array.c',
            'src/objects/process.c','src/objects/string.c']

main_srcs=['src/main.c']

luna_runtime=shared_library('lunarun',
    parser_srcs+ds_srcs+runtime_srcs,
    include_directories:includedir,
    link_args:['-lm'],
    install:true
)

install_headers(
    ['include/ast.h','include/eval.h','include/heap.h',
    'include/luna.h','include/stb_ds.h','include/operator.h',
    'include/tokenizer.h','include/value.h',
    'include/worklist.h','include/ansi_color_codes.h',
    'include/arena.h','include/parser.h','include/strbuf.h',
    'include/string_interner.h'],
    subdir : 'luna'
)

install_headers(
    ['include/bytecode/interpreter.h','include/bytecode/vm.h','include/bytecode/ir.h','include/bytecode/vm_ops.h'],
    subdir : 'luna/bytecode'
)

install_headers(
    ['include/objects/array.h','include/objects/object_prototype.h','include/objects/process.h','include/objects/string.h'],
    subdir : 'luna/objects'
)

executable('luna',
    'src/main.c',
    include_directories:includedir,
    link_with:[luna_runtime],
    install:true
)
