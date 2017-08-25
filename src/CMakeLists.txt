

set (CMAKE_CXX_STANDARD 14)

fips_begin_app(kb-gpio windowed)
    fips_files_ex(. 
                    *.cpp *.hpp
                    *.h
                    NO_RECURSE
                 )
    fips_deps(rpi-gpio)
fips_end_app()

