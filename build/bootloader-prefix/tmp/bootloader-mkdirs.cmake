# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/v5.2.3/esp-idf/components/bootloader/subproject"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/tmp"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/src/bootloader-stamp"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/src"
  "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Copias/SBC/Workspace/Proyecto/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
