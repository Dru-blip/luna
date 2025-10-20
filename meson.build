project('luna','c',
        default_options:['c_std=c2x']
)

includedir=include_directories('include')

ds_srcs = ['src/stb_ds.c','src/arena.c']
parser_srcs=['src/parser/tokenizer.c','src/parser/parser.c','src/parser/ast.c']

runtime_srcs=['src/eval.c','src/heap.c','src/value.c',]

main_srcs=['src/main.c']

luna_runtime=shared_library('lunarun',
    parser_srcs+ds_srcs+runtime_srcs,
    include_directories:includedir
)

executable('luna',
    'src/main.c',
    include_directories:includedir,
    link_with:[luna_runtime]
)
