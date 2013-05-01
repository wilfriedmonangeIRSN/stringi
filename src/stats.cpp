/* This file is part of the 'stringi' library.
 * 
 * Copyright 2013 Marek Gagolewski, Bartek Tartanus, Marcin Bujarski
 * 
 * 'stringi' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * 'stringi' is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with 'stringi'. If not, see <http://www.gnu.org/licenses/>.
 */
 
 
#include "stringi.h"

/** 
 * General statistics for a character vector
 * 
 * @param str a character vector
 * @return integer vector, see R man for details
 * @version 0.1 (Marek Gagolewski)
 */
SEXP stri_stats_general(SEXP str)
{
   str = stri_prepare_arg_string(str);
   
   enum {
      gsNumLines = 0,
      gsNumLinesNonEmpty = 1,
      gsNumChars = 2,
      gsNumCharsNonWhite = 3,
      gsAll = 4 // always == number of elements
   };

   SEXP ret;
   PROTECT(ret = allocVector(INTSXP, gsAll));
   int* stats = INTEGER(ret);
   for (int i=0; i<gsAll; ++i) stats[i] = 0;

   // @TODO: UNICODE!!!!
   R_len_t ns = LENGTH(str);
   for (R_len_t i=0; i<ns; ++i) {
      SEXP curs = STRING_ELT(str, i);
      if(curs == NA_STRING) continue; // ignore
      
      ++stats[gsNumLines]; // another line
      R_len_t     cn = LENGTH(curs);
      const char* cs = CHAR(curs);
      UChar32 c;
      bool AnyNonWhite = false;
      
      for (int j=0; j<cn; ) {
         U8_NEXT(cs, j, cn, c);
         if (c == (UChar32)'\n')
            error(MSG__NEWLINE_FOUND);
         ++stats[gsNumChars]; // another character [code point]
         // we test for UCHAR_WHITE_SPACE binary property
         if (!u_hasBinaryProperty(c, UCHAR_WHITE_SPACE)) {
            AnyNonWhite = true;
            ++stats[gsNumCharsNonWhite];
         }
      }

      if (AnyNonWhite)
         ++stats[gsNumLinesNonEmpty]; // we have a non-empty line here
   }
   
   stri__set_names(ret, gsAll, "Lines", "LinesNEmpty", "Chars", "CharsNWhite");
   UNPROTECT(1);
   return ret;
}




/** 
 * LaTeX, Kile-like statistics for a character vector
 * 
 * We use a modified LaTeX Word Count algorithm.
 * Original version from Kile 2.1.3, see http://kile.sourceforge.net/team.php
 * 
 * @param str a character vector
 * @return integer vector, see R man for details
 * @version 0.1 (Marek Gagolewski)
 */
SEXP stri_stats_latex(SEXP str)
{
   str = stri_prepare_arg_string(str);
   
   // We use a modified Kile 2.1.3 LaTeX Word Count algorithm;
   // see http://kile.sourceforge.net/team.php
   enum State {
      stStandard = 0, stComment = 1, stControlSequence = 3,
      stControlSymbol = 4, stCommand = 5, stEnvironment = 6
   };
   
   enum {
      lsCharsWord = 0,
      lsCharsCmdEnvir = 1,
      lsCharsWhite = 2,
      lsWords = 3,
      lsCmd = 4,
      lsEnvir = 5,
      lsAll = 6
   };
   
   SEXP ret;
   PROTECT(ret = allocVector(INTSXP, lsAll));
   int* stats = INTEGER(ret);
   for (int i=0; i<lsAll; ++i) stats[i] = 0;

   // @TODO: UNICODE!!!!
   R_len_t ns = LENGTH(str);
   for (R_len_t i=0; i<ns; ++i) {
      SEXP curs = STRING_ELT(str, i);
      if(curs == NA_STRING) continue; // ignore
      
      R_len_t     cn = LENGTH(curs);
      const char* cs = CHAR(curs);
      UChar32 c;
      
      int state = stStandard;
      bool word = false; // we are not in a word currently
      for (int j=0; j<cn; ) {
         U8_NEXT(cs, j, cn, c);
         
         if (c == (UChar32)'\n')
            error(MSG__NEWLINE_FOUND);
            
         UBool isLetter = u_isUAlphabetic(c); // u_hasBinaryProperty(c, UCHAR_ALPHABETIC)
         UBool isNumber = u_isdigit(c); // U_DECIMAL_DIGIT_NUMBER    Nd
         
         switch(state) {
            case stStandard:
               if (c == (UChar32)'\\') {
                  state = stControlSequence;
                  ++stats[lsCharsCmdEnvir];
                  
                  if (j < cn) {
                     // Look Ahead:
                     UChar32 cnext;
                     int jnext = j;
                     U8_NEXT(cs, jnext, cn, cnext);
                     UBool isPunctNext = u_ispunct(cnext);
                              
                     if (!isPunctNext || cnext == (UChar32)'~' || cnext == (UChar32)'^') {
                        // this is to avoid counting words like K\"ahler as two words
                        word = false;
                     }
                  }
               }
               else if (c == (UChar32)'%') {
                  state = stComment;
               }
               else {
                  if (isLetter || isNumber) {
                     // only start new word if first character is a letter
                     // (42test is still counted as a word, but 42.2 not)
                     if (isLetter && !word) {
                        word = true;
                        ++stats[lsWords];
                     }
                     ++stats[lsCharsWord];
                  }
                  else {
                     ++stats[lsCharsWhite];
                     word = false;
                  }
              }
              break; // stStandard
      
            case stControlSequence:
              if (isLetter) {
                  // "\begin{[a-zA-z]+}" is an environment, and you can't define a command like \begin
                  if (c == (UChar32)'b' && !strncmp(cs+j, "egin", 4) /* plain ASCII compare - it's OK */) {
                     ++stats[lsEnvir];
                     state = stEnvironment;
                     stats[lsCharsCmdEnvir] +=5;
                     j += 4;
                  }
                  else if (c == (UChar32)'e' && !strncmp(cs+j, "nd", 2) /* plain ASCII compare - it's OK */) {
                     stats[lsCharsCmdEnvir] +=3;
                     state = stEnvironment;
                     j += 2;
                  } // we don't count \end as new environment, this can give wrong results in selections
                  else {
                     ++stats[lsCmd];
                     ++stats[lsCharsCmdEnvir];
                     state = stCommand;
                  }
               }
               else {
                  // MG: This will also prevent counting \% as a comment (it's a percent sign)
                  ++stats[lsCmd];
                  ++stats[lsCharsCmdEnvir];
                  state = stStandard;
               }
               break;
      
            case stCommand :
               if(isLetter) {
                  ++stats[lsCharsCmdEnvir];
               }
               else if(c == (UChar32)'\\') {
                  ++stats[lsCharsCmdEnvir];
                  state = stControlSequence;
               }
               else if(c == (UChar32)'%') {
                  state = stComment;
               }
               else {
                  ++stats[lsCharsWhite];
                  state = stStandard;
               }
            break;
      
            case stEnvironment :
               if(c == (UChar32)'}') { // until we find a closing } we have an environment
                  ++stats[lsCharsCmdEnvir];
                  state = stStandard;
               }
               else if(c == (UChar32)'%') {
                  state = stComment;
               }
               else {
                  ++stats[lsCharsCmdEnvir];
               }
            break;
      
            case stComment:
               // ignore until the end - any newline will be detected
               // and the error will be thrown
            break;
      
            default:
               error("DEBUG: stri_stats_latex() - this shouldn't happen :-(");
         }
     }
   }
   
   stri__set_names(ret, lsAll, "CharsWord", "CharsCmdEnvir", "CharsWhite",
      "Words", "Cmds", "Envirs");
   UNPROTECT(1);
   return ret;
}