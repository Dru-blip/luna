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
    link_args:['-lm']
)

executable('luna',
    'src/main.c',
    include_directories:includedir,
    link_with:[luna_runtime]
)
