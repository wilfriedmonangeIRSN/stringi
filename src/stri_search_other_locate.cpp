/* This file is part of the 'stringi' package for R.
 * Copyright (c) 2013-2014, Marek Gagolewski and Bartek Tartanus
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "stri_stringi.h"
#include "stri_container_utf8_indexable.h"
#include <deque>
#include <utility>
#include <unicode/brkiter.h>
#include <unicode/rbbi.h>


/** Locate all BreakIterator boundaries
 *
 * @param str character vector
 * @param boundary single string, one of \code{character},
 * \code{line-break}, \code{sentence}, or \code{word}
 * @param locale identifier
 * @return character vector
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-22)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-23)
 *          removed "title": For Unicode 4.0 and above title boundary
 *          iteration, please use Word Boundary iterator.
 */
SEXP stri_locate_boundaries(SEXP str, SEXP boundary, SEXP locale)
{
   // @TODO: code cloning detected: almost the same as stri_split_boundaries...
   
   str = stri_prepare_arg_string(str, "str");
   boundary = stri_prepare_arg_string(boundary, "boundary");
   const char* qloc = stri__prepare_arg_locale(locale, "locale", true);
   Locale loc = Locale::createFromName(qloc);

   R_len_t str_length = LENGTH(str);
   R_len_t boundary_length = LENGTH(boundary);
   R_len_t vectorize_length = stri__recycling_rule(true, 2,
      str_length, boundary_length);

   const char* boundary_opts[] = {"character", "line-break",
      "sentence", "word", NULL};

   BreakIterator* briter = NULL;
   UText* str_text = NULL;
   STRI__ERROR_HANDLER_BEGIN
   StriContainerUTF8_indexable str_cont(str, vectorize_length);
   StriContainerUTF8 boundary_cont(boundary, vectorize_length);

   SEXP ret;
   STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));

   int last_boundary = -1;
   for (R_len_t i = boundary_cont.vectorize_init();
         i != boundary_cont.vectorize_end();
         i = boundary_cont.vectorize_next(i))
   {
      if (str_cont.isNA(i) || boundary_cont.isNA(i) || str_cont.get(i).length() == 0) {
         SET_VECTOR_ELT(ret, i, stri__matrix_NA_INTEGER(1, 2));
         continue;
      }

      // get the boundary type and open BreakIterator (if needed)
      int boundary_cur = stri__match_arg(boundary_cont.get(i).c_str(), boundary_opts);
      if (boundary_cur < 0)
         throw StriException(MSG__INCORRECT_MATCH_OPTION, "boundary");

      if (last_boundary != boundary_cur) { // otherwise reuse BreakIterator
         if (briter) { delete briter; briter = NULL; }
         last_boundary = boundary_cur;
         UErrorCode status = U_ZERO_ERROR;
         switch (boundary_cur) {
            case 0: // character
               briter = BreakIterator::createCharacterInstance(loc, status);
               break;

            case 1: // line-break
               briter = BreakIterator::createLineInstance(loc, status);
               break;

            case 2: // sentence
               briter = BreakIterator::createSentenceInstance(loc, status);
               break;

//            case 3: // title
//               briter = BreakIterator::createTitleInstance(loc, status);
//               break;

            case 3: // word
               briter = BreakIterator::createWordInstance(loc, status);
               break;
         }
         if (U_FAILURE(status))
            throw StriException(status); // briter will be deleted automagically
      }

      // get the current string
      UErrorCode status = U_ZERO_ERROR;
      const char* str_cur_s = str_cont.get(i).c_str();
      str_text = utext_openUTF8(str_text, str_cur_s, str_cont.get(i).length(), &status);
      if (U_FAILURE(status))
         throw StriException(status);
      briter->setText(str_text, status);
      if (U_FAILURE(status))
         throw StriException(status);

      deque< pair<R_len_t, R_len_t> > occurences;
      int last_match = briter->first(), match;
      while ((match = briter->next()) != BreakIterator::DONE) {
         occurences.push_back(pair<R_len_t, R_len_t>(last_match, match));
         last_match = match;
      }

      R_len_t noccurences = (R_len_t)occurences.size();
      SEXP ans;
      STRI__PROTECT(ans = Rf_allocMatrix(INTSXP, noccurences, 2));
      int* ans_tab = INTEGER(ans);
      deque< pair<R_len_t, R_len_t> >::iterator iter = occurences.begin();
      for (R_len_t j = 0; iter != occurences.end(); ++iter, ++j) {
         pair<R_len_t, R_len_t> match = *iter;
         ans_tab[j]             = match.first;
         ans_tab[j+noccurences] = match.second;
      }

      // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
      str_cont.UTF8_to_UChar32_index(i, ans_tab,
            ans_tab+noccurences, noccurences,
            1, // 0-based index -> 1-based
            0  // end returns position of next character after match
      );
      SET_VECTOR_ELT(ret, i, ans);
      STRI__UNPROTECT(1);
   }

   if (briter) { delete briter; briter = NULL; }
   if (str_text) { utext_close(str_text); str_text = NULL; }
   stri__locate_set_dimnames_list(ret);
   STRI__UNPROTECT_ALL
   return ret;
   STRI__ERROR_HANDLER_END({
      if (briter) { delete briter; briter = NULL; }
      if (str_text) { utext_close(str_text); str_text = NULL; }
   })
}


/** Locate words using a BreakIterator
 *
 * @param str character vector
 * @param locale identifier
 * @return character vector
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-23)
 *
 */
SEXP stri_locate_words(SEXP str, SEXP locale)
{
   // @TODO: code cloning detected: almost the same as stri_extract_words...
   
   str = stri_prepare_arg_string(str, "str");
   const char* qloc = stri__prepare_arg_locale(locale, "locale", true);
   Locale loc = Locale::createFromName(qloc);

   R_len_t str_length = LENGTH(str);
   R_len_t vectorize_length = str_length;

   RuleBasedBreakIterator* briter = NULL;
   UText* str_text = NULL;
   STRI__ERROR_HANDLER_BEGIN

   UErrorCode status = U_ZERO_ERROR;
   briter = (RuleBasedBreakIterator*)BreakIterator::createWordInstance(loc, status);
   if (U_FAILURE(status))
      throw StriException(status);

   StriContainerUTF8_indexable str_cont(str, vectorize_length);

   SEXP ret;
   STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));

   for (R_len_t i = 0; i < vectorize_length; ++i)
   {
      if (str_cont.isNA(i)) {
         SET_VECTOR_ELT(ret, i, stri__matrix_NA_INTEGER(1, 2));
         continue;
      }

      // get the current string
      UErrorCode status = U_ZERO_ERROR;
      const char* str_cur_s = str_cont.get(i).c_str();
      str_text = utext_openUTF8(str_text, str_cur_s, str_cont.get(i).length(), &status);
      if (U_FAILURE(status))
         throw StriException(status);
      briter->setText(str_text, status);
      if (U_FAILURE(status))
         throw StriException(status);

      deque< pair<R_len_t,R_len_t> > occurences;
      R_len_t match, last_match = briter->first();
      while ((match = briter->next()) != BreakIterator::DONE) {
         int breakType = briter->getRuleStatus();
         if (breakType != UBRK_WORD_NONE)
            occurences.push_back(pair<R_len_t, R_len_t>(last_match, match));
         last_match = match;
      }

      R_len_t noccurences = (R_len_t)occurences.size();
      if (noccurences <= 0) { // no matches at all
         SET_VECTOR_ELT(ret, i, stri__matrix_NA_INTEGER(1, 2));
         continue;
      }

      SEXP ans;
      STRI__PROTECT(ans = Rf_allocMatrix(INTSXP, noccurences, 2));
      int* ans_tab = INTEGER(ans);
      deque< pair<R_len_t, R_len_t> >::iterator iter = occurences.begin();
      for (R_len_t j = 0; iter != occurences.end(); ++iter, ++j) {
         pair<R_len_t, R_len_t> match = *iter;
         ans_tab[j]             = match.first;
         ans_tab[j+noccurences] = match.second;
      }

      // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
      str_cont.UTF8_to_UChar32_index(i, ans_tab,
            ans_tab+noccurences, noccurences,
            1, // 0-based index -> 1-based
            0  // end returns position of next character after match
      );
      SET_VECTOR_ELT(ret, i, ans);
      STRI__UNPROTECT(1);
   }

   if (briter) { delete briter; briter = NULL; }
   if (str_text) { utext_close(str_text); str_text = NULL; }
   stri__locate_set_dimnames_list(ret);
   STRI__UNPROTECT_ALL
   return ret;
   STRI__ERROR_HANDLER_END({
      if (briter) { delete briter; briter = NULL; }
      if (str_text) { utext_close(str_text); str_text = NULL; }
   })
}