/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2015 Kitware, Inc.

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
/*-------------------------------------------------------------------------
  Portions of this source have been derived from the 'bindexplib' tool
  provided by the CERN ROOT Data Analysis Framework project (root.cern.ch).
  Permission has been granted by Pere Mato <pere.mato@cern.ch> to distribute
  this derived work under the CMake license.
-------------------------------------------------------------------------*/

/*
*----------------------------------------------------------------------
* Program:  dumpexts.exe
* Author:   Gordon Chaffee
*
* History:  The real functionality of this file was written by
*           Matt Pietrek in 1993 in his pedump utility.  I've
*           modified it to dump the externals in a bunch of object
*           files to create a .def file.
*
* Notes:    Visual C++ puts an underscore before each exported symbol.
*           This file removes them.  I don't know if this is a problem
*           this other compilers.  If _MSC_VER is defined,
*           the underscore is removed.  If not, it isn't.  To get a
*           full dump of an object file, use the -f option.  This can
*           help determine the something that may be different with a
*           compiler other than Visual C++.
*   ======================================
* Corrections (Axel 2006-04-04):
*   Conversion to C++. Mostly.
*
 * Extension (Axel 2006-03-15)
 *    As soon as an object file contains an /EXPORT directive (which
 *    is generated by the compiler when a symbol is declared as
 *    declspec(dllexport)) no to-be-exported symbols are printed,
 *    as the linker will see these directives, and if those directives
 *    are present we only export selectively (i.e. we trust the
 *    programmer).
 *
 *   ======================================
*   ======================================
* Corrections (Valery Fine 23/02/98):
*
*           The "(vector) deleting destructor" MUST not be exported
*           To recognize it the following test are introduced:
*  "@@UAEPAXI@Z"  scalar deleting dtor
*  "@@QAEPAXI@Z"  vector deleting dtor
*  "AEPAXI@Z"     vector deleting dtor with thunk adjustor
*   ======================================
* Corrections (Valery Fine 12/02/97):
*
*    It created a wrong EXPORTS for the global pointers and constants.
*    The Section Header has been involved to discover the missing information
*    Now the pointers are correctly supplied  supplied with "DATA" descriptor
*        the constants  with no extra descriptor.
*
* Corrections (Valery Fine 16/09/96):
*
*     It didn't work for C++ code with global variables and class definitons
*     The DumpExternalObject function has been introduced to generate .DEF file
*
* Author:   Valery Fine 16/09/96  (E-mail: fine@vxcern.cern.ch)
*----------------------------------------------------------------------
*/

static char sccsid[] = "@(#) winDumpExts.c 1.2 95/10/03 15:27:34";

#include <cmsys/Encoding.hxx>
#include <windows.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <iostream>

/*
*  The names of the first group of possible symbol table storage classes
*/
char * SzStorageClass1[] = {
   "NULL","AUTOMATIC","EXTERNAL","STATIC","REGISTER","EXTERNAL_DEF","LABEL",
   "UNDEFINED_LABEL","MEMBER_OF_STRUCT","ARGUMENT","STRUCT_TAG",
   "MEMBER_OF_UNION","UNION_TAG","TYPE_DEFINITION","UNDEFINED_STATIC",
   "ENUM_TAG","MEMBER_OF_ENUM","REGISTER_PARAM","BIT_FIELD"
};

/*
* The names of the second group of possible symbol table storage classes
*/
char * SzStorageClass2[] = {
   "BLOCK","FUNCTION","END_OF_STRUCT","FILE","SECTION","WEAK_EXTERNAL"
};

/*
+ * Utility func, strstr with size
+ */
const char* StrNStr(const char* start, const char* find, size_t &size) {
   size_t len;
   const char* hint;

   if (!start || !find || !size) {
      size = 0;
      return 0;
   }
   len = strlen(find);

   while (hint = (const char*) memchr(start, find[0], size-len+1)) {
      size -= (hint - start);
      if (!strncmp(hint, find, len))
         return hint;
      start = hint + 1;
   }

   size = 0;
   return 0;
}

/*
 *----------------------------------------------------------------------
 * HaveExportedObjects --
 *
 *      Returns >0 if export directives (declspec(dllexport)) exist.
 *
 *----------------------------------------------------------------------
 */
int
HaveExportedObjects(PIMAGE_FILE_HEADER pImageFileHeader,
                    PIMAGE_SECTION_HEADER pSectionHeaders, FILE *fout)
{
    static int fImportFlag = 0;  /*  The status is nor defined yet */
    WORD i;
    size_t size;
    char foundExports;
    const char * rawdata;

    PIMAGE_SECTION_HEADER pDirectivesSectionHeader;

    if (fImportFlag) return 1;

    i = 0;
    foundExports = 0;
    pDirectivesSectionHeader = 0;
    for(i = 0; (i < pImageFileHeader->NumberOfSections &&
                !pDirectivesSectionHeader); i++)
       if (!strncmp((const char*)&pSectionHeaders[i].Name[0], ".drectve",8))
          pDirectivesSectionHeader = &pSectionHeaders[i];
   if (!pDirectivesSectionHeader) return 0;

    rawdata=(const char*)
      pImageFileHeader+pDirectivesSectionHeader->PointerToRawData;
    if (!pDirectivesSectionHeader->PointerToRawData || !rawdata) return 0;

    size = pDirectivesSectionHeader->SizeOfRawData;
    const char* posImportFlag = rawdata;
    while ((posImportFlag = StrNStr(posImportFlag, " /EXPORT:", size))) {
       const char* lookingForDict = posImportFlag + 9;
       if (!strncmp(lookingForDict, "_G__cpp_",8) ||
           !strncmp(lookingForDict, "_G__set_cpp_",12)) {
          posImportFlag = lookingForDict;
          continue;
       }

       const char* lookingForDATA = posImportFlag + 9;
       while (*(++lookingForDATA) && *lookingForDATA != ' ');
       lookingForDATA -= 5;
       // ignore DATA exports
       if (strncmp(lookingForDATA, ",DATA", 5)) break;
       posImportFlag = lookingForDATA + 5;
    }
    fImportFlag = (int)posImportFlag;
    return fImportFlag;
}



/*
 *----------------------------------------------------------------------
* DumpExternalsObjects --
*
*      Dumps a COFF symbol table from an EXE or OBJ.  We only use
*      it to dump tables from OBJs.
*----------------------------------------------------------------------
*/
void
DumpExternalsObjects(PIMAGE_SYMBOL pSymbolTable,
                     PIMAGE_SECTION_HEADER pSectionHeaders,
                     FILE *fout, DWORD_PTR cSymbols)
{
   unsigned i;
   PSTR stringTable;
   std::string symbol;
   DWORD SectChar;
   static int fImportFlag = -1;  /*  The status is nor defined yet */

   /*
   * The string table apparently starts right after the symbol table
   */
   stringTable = (PSTR)&pSymbolTable[cSymbols];

   for ( i=0; i < cSymbols; i++ ) {
      if (pSymbolTable->SectionNumber > 0 &&
          ( pSymbolTable->Type == 0x20 || pSymbolTable->Type == 0x0)) {
         if (pSymbolTable->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            /*
            *    The name of the Function entry points
            */
            if (pSymbolTable->N.Name.Short != 0) {
               symbol = "";
               symbol.insert(0, (const char *)pSymbolTable->N.ShortName, 8);
            } else {
               symbol = stringTable + pSymbolTable->N.Name.Long;
            }

            while (isspace(symbol[0])) symbol.erase(0,1);
            if (symbol[0] == '_') symbol.erase(0,1);
            if (fImportFlag) {
               fImportFlag = 0;
               fprintf(fout,"EXPORTS \n");
            }
            /*
            Check whether it is "Scalar deleting destructor" and
            "Vector deleting destructor"
            */
            const char *scalarPrefix = "??_G";
            const char *vectorPrefix = "??_E";
            if (symbol.compare(0, 4, scalarPrefix) &&
               symbol.compare(0, 4, vectorPrefix) &&
               symbol.find("real@") == std::string::npos)
            {
               SectChar =
                pSectionHeaders[pSymbolTable->SectionNumber-1].Characteristics;
               if (!pSymbolTable->Type  && (SectChar & IMAGE_SCN_MEM_WRITE)) {
                  // Read only (i.e. constants) must be excluded
                  fprintf(fout, "\t%s \t DATA\n", symbol.c_str());
               } else {
                  if ( pSymbolTable->Type  ||
                       !(SectChar & IMAGE_SCN_MEM_READ)) {
                     fprintf(fout, "\t%s\n", symbol.c_str());
                  } else {
                     //                    printf(" strange symbol: %s \n",s);
                  }
               }
            }
         }
      }
      else if (pSymbolTable->SectionNumber == IMAGE_SYM_UNDEFINED &&
               !pSymbolTable->Type && 0) {
         /*
         *    The IMPORT global variable entry points
         */
         if (pSymbolTable->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            symbol = stringTable + pSymbolTable->N.Name.Long;
            while (isspace(symbol[0]))  symbol.erase(0,1);
            if (symbol[0] == '_') symbol.erase(0,1);
            if (!fImportFlag) {
               fImportFlag = 1;
               fprintf(fout,"IMPORTS \n");
            }
            fprintf(fout, "\t%s DATA \n", symbol.c_str()+1);
         }
      }

      /*
      * Take into account any aux symbols
      */
      i += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable++;
   }
}

/*
*----------------------------------------------------------------------
* DumpObjFile --
*
*      Dump an object file--either a full listing or just the exported
*      symbols.
*----------------------------------------------------------------------
*/
void
DumpObjFile(PIMAGE_FILE_HEADER pImageFileHeader, FILE *fout)
{
   PIMAGE_SYMBOL PCOFFSymbolTable;
   PIMAGE_SECTION_HEADER PCOFFSectionHeaders;
   DWORD_PTR COFFSymbolCount;

   PCOFFSymbolTable = (PIMAGE_SYMBOL)
      ((DWORD_PTR)pImageFileHeader + pImageFileHeader->PointerToSymbolTable);
   COFFSymbolCount = pImageFileHeader->NumberOfSymbols;

   PCOFFSectionHeaders = (PIMAGE_SECTION_HEADER)
      ((DWORD_PTR)pImageFileHeader          +
      IMAGE_SIZEOF_FILE_HEADER +
      pImageFileHeader->SizeOfOptionalHeader);


   int haveExports = HaveExportedObjects(pImageFileHeader,
                                         PCOFFSectionHeaders, fout);
   if (!haveExports)
       DumpExternalsObjects(PCOFFSymbolTable, PCOFFSectionHeaders,
                            fout, COFFSymbolCount);
}

/*
*----------------------------------------------------------------------
* DumpFile --
*
*      Open up a file, memory map it, and call the appropriate
*      dumping routine
*----------------------------------------------------------------------
*/
void
DumpFile(const char* filename, FILE *fout)
{
   HANDLE hFile;
   HANDLE hFileMapping;
   LPVOID lpFileBase;
   PIMAGE_DOS_HEADER dosHeader;

   hFile = CreateFileW(cmsys::Encoding::ToWide(filename).c_str(),
                       GENERIC_READ, FILE_SHARE_READ, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

   if (hFile == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "Couldn't open file with CreateFile()\n");
      return;
   }

   hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
   if (hFileMapping == 0) {
      CloseHandle(hFile);
      fprintf(stderr, "Couldn't open file mapping with CreateFileMapping()\n");
      return;
   }

   lpFileBase = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
   if (lpFileBase == 0) {
      CloseHandle(hFileMapping);
      CloseHandle(hFile);
      fprintf(stderr, "Couldn't map view of file with MapViewOfFile()\n");
      return;
   }

   dosHeader = (PIMAGE_DOS_HEADER)lpFileBase;
   if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
      fprintf(stderr, "File is an executable.  I don't dump those.\n");
      return;
   }
   /* Does it look like a i386 COFF OBJ file??? */
   else if (
           ((dosHeader->e_magic == IMAGE_FILE_MACHINE_I386) ||
            (dosHeader->e_magic == IMAGE_FILE_MACHINE_AMD64))
           && (dosHeader->e_sp == 0)
           ) {
      /*
      * The two tests above aren't what they look like.  They're
      * really checking for IMAGE_FILE_HEADER.Machine == i386 (0x14C)
      * and IMAGE_FILE_HEADER.SizeOfOptionalHeader == 0;
      */
      DumpObjFile((PIMAGE_FILE_HEADER) lpFileBase, fout);
   } else {
      printf("unrecognized file format\n");
   }
   UnmapViewOfFile(lpFileBase);
   CloseHandle(hFileMapping);
   CloseHandle(hFile);
}
