IF(CMAKE_COMPILER_IS_GNUG77)
  SET (CMAKE_Fortran_FLAGS_INIT "")
  SET (CMAKE_Fortran_FLAGS_DEBUG_INIT "-g")
  SET (CMAKE_Fortran_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")
  SET (CMAKE_Fortran_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")
  SET (CMAKE_Fortran_FLAGS_RELWITHDEBINFO_INIT "-O2 -g")
ENDIF(CMAKE_COMPILER_IS_GNUG77)
