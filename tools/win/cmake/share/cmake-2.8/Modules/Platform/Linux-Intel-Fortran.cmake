INCLUDE(Platform/Linux-Intel)
IF(XIAR)
  # INTERPROCEDURAL_OPTIMIZATION
  SET(CMAKE_Fortran_COMPILE_OPTIONS_IPO -ipo)
  SET(CMAKE_Fortran_CREATE_STATIC_LIBRARY_IPO
    "${XIAR} cr <TARGET> <LINK_FLAGS> <OBJECTS> "
    "${XIAR} -s <TARGET> ")
ENDIF(XIAR)

SET(CMAKE_SHARED_LIBRARY_Fortran_FLAGS "-fPIC")
SET(CMAKE_SHARED_LIBRARY_CREATE_Fortran_FLAGS "-shared -i_dynamic -nofor_main")
SET(CMAKE_SHARED_LIBRARY_LINK_Fortran_FLAGS "-i_dynamic")
SET(CMAKE_SHARED_LIBRARY_RUNTIME_Fortran_FLAG "-Wl,-rpath,")
SET(CMAKE_SHARED_LIBRARY_RUNTIME_Fortran_FLAG_SEP ":")
SET(CMAKE_SHARED_LIBRARY_SONAME_Fortran_FLAG "-Wl,-soname,")
SET(CMAKE_DL_LIBS "dl")