/**********************************************************************

  id.c - 

  $Author: ko1 $
  created at: Thu Jul 12 04:37:51 2007

  Copyright (C) 2004-2007 Koichi Sasada

**********************************************************************/

#include "ruby/ruby.h"

#define extern
#include "id.h"
#undef extern

void
Init_id(void)
{
    /* Symbols */
    symIFUNC = ID2SYM(rb_intern("<IFUNC>"));
    symCFUNC = ID2SYM(rb_intern("<CFUNC>"));

    /* IDs */
    idPLUS = rb_intern("+");
    idMINUS = rb_intern("-");
    idMULT = rb_intern("*");
    idDIV = rb_intern("/");
    idMOD = rb_intern("%");
    idLT = rb_intern("<");
    idLTLT = rb_intern("<<");
    idLE = rb_intern("<=");
    idGT = rb_intern(">");
    idGE = rb_intern(">=");
    idEq = rb_intern("==");
    idEqq = rb_intern("===");
    idBackquote = rb_intern("`");
    idEqTilde = rb_intern("=~");
    idNot = rb_intern("!");
    idNeq = rb_intern("!=");

#if WITH_OBJC
    selPLUS = sel_registerName("+:");
    selMINUS = sel_registerName("-:");
    selMULT = sel_registerName("*:");
    selDIV = sel_registerName("/:");
    selMOD = sel_registerName("%:");
    selEq = sel_registerName("==:");
    selNeq = sel_registerName("!=:");
    selLT = sel_registerName("<:");
    selLE = sel_registerName("<=:");
    selGT = sel_registerName(">:");
    selGE = sel_registerName(">=:");
    selLTLT = sel_registerName("<<:");
    selAREF = sel_registerName("[]:");
    selASET = sel_registerName("[]=:");
    selLength = sel_registerName("length");
    selSucc = sel_registerName("succ");
    selNot = sel_registerName("!");
    selInit = sel_registerName("init");
    selCopy = sel_registerName("copy");
    sel_ignored = sel_registerName("retain");
//    assert(sel_ignored == sel_registerName("release"));
    sel_zone = sel_registerName("zone");
#endif

    idAREF = rb_intern("[]");
    idASET = rb_intern("[]=");

    idEach = rb_intern("each");
    idTimes = rb_intern("times");
    idLength = rb_intern("length");
    idLambda = rb_intern("lambda");
    idIntern = rb_intern("intern");
    idGets = rb_intern("gets");
    idSucc = rb_intern("succ");
    idEnd = rb_intern("end");
    idRangeEachLT = rb_intern("Range#each#LT");
    idRangeEachLE = rb_intern("Range#each#LE");
    idArrayEach = rb_intern("Array#each");
    idMethodMissing = rb_intern("method_missing");

    idThrowState = rb_intern("#__ThrowState__");

    idBitblt = rb_intern("bitblt");
    idAnswer = rb_intern("the_answer_to_life_the_universe_and_everything");

    idSend = rb_intern("send");
    id__send__ = rb_intern("__send__");

    idRespond_to = rb_intern("respond_to?");
    idInitialize = rb_intern("initialize");

    idIncludedModules = rb_intern("__included_modules__");
    idIncludedInClasses = rb_intern("__included_in_classes__");
    idAncestors = rb_intern("__ancestors__");
}
