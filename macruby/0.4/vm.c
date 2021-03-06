/**********************************************************************

  vm.c -

  $Author: nobu $

  Copyright (C) 2004-2007 Koichi Sasada

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "ruby/encoding.h"
#include "gc.h"

#include "insnhelper.h"
#include "vm_insnhelper.c"
#include "vm_eval.c"

#define BUFSIZE 0x100
#define PROCDEBUG 0

VALUE rb_cVM;
VALUE rb_cThread;
VALUE rb_cEnv;

VALUE ruby_vm_global_state_version = 1;
rb_thread_t *ruby_current_thread = 0;
rb_vm_t *ruby_current_vm = 0;

void vm_analysis_operand(int insn, int n, VALUE op);
void vm_analysis_register(int reg, int isset);
void vm_analysis_insn(int insn);

#if OPT_STACK_CACHING
static VALUE finish_insn_seq[1] = { BIN(finish_SC_ax_ax) };
#elif OPT_CALL_THREADED_CODE
static VALUE const finish_insn_seq[1] = { 0 };
#else
static VALUE finish_insn_seq[1] = { BIN(finish) };
#endif

void
rb_vm_change_state(void)
{
    INC_VM_STATE_VERSION();
}

/* control stack frame */

static inline VALUE
rb_vm_set_finish_env(rb_thread_t * th)
{
    vm_push_frame(th, 0, FRAME_MAGIC_FINISH,
		  Qnil, th->cfp->lfp[0], 0,
		  th->cfp->sp, 0, 1);
    th->cfp->pc = (VALUE *)&finish_insn_seq[0];
    return Qtrue;
}

static void
vm_set_top_stack(rb_thread_t * th, VALUE iseqval)
{
    rb_iseq_t *iseq;
    GetISeqPtr(iseqval, iseq);

    if (iseq->type != ISEQ_TYPE_TOP) {
	rb_raise(rb_eTypeError, "Not a toplevel InstructionSequence");
    }

    /* for return */
    rb_vm_set_finish_env(th);

    vm_push_frame(th, iseq, FRAME_MAGIC_TOP,
		  th->top_self, 0, iseq->iseq_encoded,
		  th->cfp->sp, 0, iseq->local_size);
}

static void
vm_set_eval_stack(rb_thread_t * th, VALUE iseqval, const NODE *cref)
{
    rb_iseq_t *iseq;
    rb_block_t * const block = th->base_block;
    GetISeqPtr(iseqval, iseq);

    /* for return */
    rb_vm_set_finish_env(th);
    vm_push_frame(th, iseq, FRAME_MAGIC_EVAL, block->self,
		  GC_GUARDED_PTR(block->dfp), iseq->iseq_encoded,
		  th->cfp->sp, block->lfp, iseq->local_size);

    if (cref) {
	GC_WB(&th->cfp->dfp[-1], (VALUE)cref);
    }
}

rb_control_frame_t *
vm_get_ruby_level_cfp(rb_thread_t *th, rb_control_frame_t *cfp)
{
    while (!RUBY_VM_CONTROL_FRAME_STACK_OVERFLOW_P(th, cfp)) {
	if (RUBY_VM_NORMAL_ISEQ_P(cfp->iseq)) {
	    return cfp;
	}
	cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
    }
    return 0;
}

/* Env */

static void
env_free(void * const ptr)
{
    RUBY_FREE_ENTER("env");
    if (ptr) {
	const rb_env_t * const env = ptr;
	RUBY_FREE_UNLESS_NULL(env->env);
	ruby_xfree(ptr);
    }
    RUBY_FREE_LEAVE("env");
}

static void
env_mark(void * const ptr)
{
    RUBY_MARK_ENTER("env");
    if (ptr) {
	const rb_env_t * const env = ptr;

	if (env->env) {
	    /* TODO: should mark more restricted range */
	    RUBY_GC_INFO("env->env\n");
	    rb_gc_mark_locations(env->env, env->env + env->env_size);
	}

	RUBY_GC_INFO("env->prev_envval\n");
	RUBY_MARK_UNLESS_NULL(env->prev_envval);
	RUBY_MARK_UNLESS_NULL(env->block.self);
	RUBY_MARK_UNLESS_NULL(env->block.proc);

	if (env->block.iseq) {
	    if (BUILTIN_TYPE(env->block.iseq) == T_NODE) {
		RUBY_MARK_UNLESS_NULL((VALUE)env->block.iseq);
	    }
	    else {
		RUBY_MARK_UNLESS_NULL(env->block.iseq->self);
	    }
	}
    }
    RUBY_MARK_LEAVE("env");
}

static VALUE
env_alloc(void)
{
    VALUE obj;
    rb_env_t *env;
    obj = Data_Make_Struct(rb_cEnv, rb_env_t, env_mark, env_free, env);
    env->env = 0;
    env->prev_envval = 0;
    env->block.iseq = 0;
    return obj;
}

static VALUE check_env_value(VALUE envval);

static int
check_env(rb_env_t * const env)
{
    printf("---\n");
    printf("envptr: %p\n", &env->block.dfp[0]);
    printf("orphan: %p\n", (void *)env->block.dfp[1]);
    printf("inheap: %p\n", (void *)env->block.dfp[2]);
    printf("envval: %10p ", (void *)env->block.dfp[3]);
    dp(env->block.dfp[3]);
    printf("penvv : %10p ", (void *)env->block.dfp[4]);
    dp(env->block.dfp[4]);
    printf("lfp:    %10p\n", env->block.lfp);
    printf("dfp:    %10p\n", env->block.dfp);
    if (env->block.dfp[4]) {
	printf(">>\n");
	check_env_value(env->block.dfp[4]);
	printf("<<\n");
    }
    return 1;
}

static VALUE
check_env_value(VALUE envval)
{
    rb_env_t *env;
    GetEnvPtr(envval, env);

    if (check_env(env)) {
	return envval;
    }
    rb_bug("invalid env");
    return Qnil;		/* unreachable */
}

static VALUE
vm_make_env_each(rb_thread_t * const th, rb_control_frame_t * const cfp,
		 VALUE *envptr, VALUE * const endptr)
{
    VALUE envval, penvval = 0;
    rb_env_t *env;
    VALUE *nenvptr;
    int i, local_size;

    if (ENV_IN_HEAP_P(th, envptr)) {
	return ENV_VAL(envptr);
    }

    if (envptr != endptr) {
	VALUE *penvptr = GC_GUARDED_PTR_REF(*envptr);
	rb_control_frame_t *pcfp = cfp;

	if (ENV_IN_HEAP_P(th, penvptr)) {
	    penvval = ENV_VAL(penvptr);
	}
	else {
	    while (pcfp->dfp != penvptr) {
		pcfp++;
		if (pcfp->dfp == 0) {
		    SDR();
		    rb_bug("invalid dfp");
		}
	    }
	    penvval = vm_make_env_each(th, pcfp, penvptr, endptr);
	    cfp->lfp = pcfp->lfp;
	    *envptr = GC_GUARDED_PTR(pcfp->dfp);
	}
    }

    /* allocate env */
    envval = env_alloc();
    GetEnvPtr(envval, env);

    if (!RUBY_VM_NORMAL_ISEQ_P(cfp->iseq)) {
	local_size = 2;
    }
    else {
	local_size = cfp->iseq->local_size;
    }

    env->env_size = local_size + 1 + 2;
    env->local_size = local_size;
    GC_WB(&env->env, ALLOC_N(VALUE, env->env_size));
    GC_WB(&env->prev_envval, penvval);

    for (i = 0; i <= local_size; i++) {
	env->env[i] = envptr[-local_size + i];
#if 0
	fprintf(stderr, "%2d ", &envptr[-local_size + i] - th->stack); dp(env->env[i]);
	if (RUBY_VM_NORMAL_ISEQ_P(cfp->iseq)) {
	    /* clear value stack for GC */
	    envptr[-local_size + i] = 0;
	}
#endif
    }

    *envptr = envval;		/* GC mark */
    nenvptr = &env->env[i - 1];
    nenvptr[1] = envval;	/* frame self */
    nenvptr[2] = penvval;	/* frame prev env object */

    /* reset lfp/dfp in cfp */
    cfp->dfp = nenvptr;
    if (envptr == endptr) {
	cfp->lfp = nenvptr;
    }

    /* as Binding */
    env->block.self = cfp->self;
    env->block.lfp = cfp->lfp;
    env->block.dfp = cfp->dfp;
    env->block.iseq = cfp->iseq;

    if (!RUBY_VM_NORMAL_ISEQ_P(cfp->iseq)) {
	/* TODO */
	env->block.iseq = 0;
    }
    return envval;
}

static int
collect_local_variables_in_env(rb_env_t * const env, const VALUE ary)
{
    int i;
    for (i = 0; i < env->block.iseq->local_table_size; i++) {
	ID lid = env->block.iseq->local_table[i];
	if (lid) {
	    rb_ary_push(ary, ID2SYM(lid));
	}
    }
    if (env->prev_envval) {
	rb_env_t *prevenv;
	GetEnvPtr(env->prev_envval, prevenv);
	collect_local_variables_in_env(prevenv, ary);
    }
    return 0;
}

int
vm_collect_local_variables_in_heap(rb_thread_t * const th,
				   VALUE * const dfp, const VALUE ary)
{
    if (ENV_IN_HEAP_P(th, dfp)) {
	rb_env_t *env;
	GetEnvPtr(ENV_VAL(dfp), env);
	collect_local_variables_in_env(env, ary);
	return 1;
    }
    else {
	return 0;
    }
}

VALUE
vm_make_env_object(rb_thread_t * th, rb_control_frame_t *cfp)
{
    VALUE envval;

    if (VM_FRAME_FLAG(cfp->flag) == FRAME_MAGIC_FINISH) {
	/* for method_missing */
	cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
    }

    envval = vm_make_env_each(th, cfp, cfp->dfp, cfp->lfp);

    if (PROCDEBUG) {
	check_env_value(envval);
    }

    return envval;
}

void
vm_stack_to_heap(rb_thread_t * const th)
{
    rb_control_frame_t *cfp = th->cfp;
    while ((cfp = vm_get_ruby_level_cfp(th, cfp)) != 0) {
	vm_make_env_object(th, cfp);
	cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
    }
}

/* Proc */

static VALUE
vm_make_proc_from_block(rb_thread_t *th, rb_control_frame_t *cfp,
			rb_block_t *block)
{
    VALUE procval;
    rb_control_frame_t *bcfp;
    VALUE *bdfp;		/* to gc mark */

    if (block->proc) {
	return block->proc;
    }

    bcfp = RUBY_VM_GET_CFP_FROM_BLOCK_PTR(block);
    bdfp = bcfp->dfp;
    procval = vm_make_proc(th, bcfp, block);
    GC_WB(&block->proc, procval); 
    return procval;
}

VALUE
vm_make_proc(rb_thread_t *th,
	     rb_control_frame_t *cfp, const rb_block_t *block)
{
    VALUE procval, envval, blockprocval = 0;
    rb_proc_t *proc;

    if (GC_GUARDED_PTR_REF(cfp->lfp[0])) {
	if (!RUBY_VM_CLASS_SPECIAL_P(cfp->lfp[0])) {
	    rb_proc_t *p;

	    blockprocval = vm_make_proc_from_block(
		th, cfp, (rb_block_t *)GC_GUARDED_PTR_REF(*cfp->lfp));

	    GetProcPtr(blockprocval, p);
	    GC_WB(cfp->lfp, GC_GUARDED_PTR(&p->block));
	}
    }
    envval = vm_make_env_object(th, cfp);

    if (PROCDEBUG) {
	check_env_value(envval);
    }
    procval = rb_proc_alloc(rb_cProc);
    GetProcPtr(procval, proc);
    GC_WB(&proc->blockprocval, blockprocval);
    GC_WB(&proc->block.self, block->self);
    GC_WB(&proc->block.lfp, block->lfp);
    GC_WB(&proc->block.dfp, block->dfp);
    GC_WB(&proc->block.iseq, block->iseq);
    GC_WB(&proc->block.proc, procval);
    GC_WB(&proc->envval, envval);
    proc->safe_level = th->safe_level;

    if (VMDEBUG) {
	if (th->stack < block->dfp && block->dfp < th->stack + th->stack_size) {
	    rb_bug("invalid ptr: block->dfp");
	}
	if (th->stack < block->lfp && block->lfp < th->stack + th->stack_size) {
	    rb_bug("invalid ptr: block->lfp");
	}
    }

    return procval;
}

/* C -> Ruby: block */

static inline VALUE
invoke_block_from_c(rb_thread_t *th, const rb_block_t *block,
		    VALUE self, int argc, const VALUE *argv,
		    const rb_block_t *blockptr, const NODE *cref)
{
    if (BUILTIN_TYPE(block->iseq) != T_NODE) {
	const rb_iseq_t *iseq = block->iseq;
	const rb_control_frame_t *cfp = th->cfp;
	int i, opt_pc, arg_size = iseq->arg_size;
	int type = block_proc_is_lambda(block->proc) ? FRAME_MAGIC_LAMBDA : FRAME_MAGIC_BLOCK;

	rb_vm_set_finish_env(th);

	CHECK_STACK_OVERFLOW(cfp, argc + iseq->stack_max);

	for (i=0; i<argc; i++) {
	    cfp->sp[i] = argv[i];
	}

	opt_pc = vm_yield_setup_args(th, iseq, argc, cfp->sp, blockptr,
				     type == FRAME_MAGIC_LAMBDA);

	vm_push_frame(th, iseq, type,
		      self, GC_GUARDED_PTR(block->dfp),
		      iseq->iseq_encoded + opt_pc, cfp->sp + arg_size, block->lfp,
		      iseq->local_size - arg_size);

	if (cref) {
	    GC_WB(&th->cfp->dfp[-1], cref);
	}

	return vm_eval_body(th);
    }
    else {
	return vm_yield_with_cfunc(th, block, self, argc, argv);
    }
}

static inline const rb_block_t *
check_block(rb_thread_t *th)
{
    const rb_block_t *blockptr = GC_GUARDED_PTR_REF(th->cfp->lfp[0]);

    if (blockptr == 0) {
	vm_localjump_error("no block given", Qnil, 0);
    }

    return blockptr;
}

static inline VALUE
vm_yield_with_cref(rb_thread_t *th, int argc, const VALUE *argv, const NODE *cref)
{
    const rb_block_t *blockptr = check_block(th);
    return invoke_block_from_c(th, blockptr, blockptr->self, argc, argv, 0, cref);
}

static inline VALUE
vm_yield(rb_thread_t *th, int argc, const VALUE *argv)
{
    const rb_block_t *blockptr = check_block(th);
    return invoke_block_from_c(th, blockptr, blockptr->self, argc, argv, 0, 0);
}

VALUE
vm_invoke_proc(rb_thread_t *th, rb_proc_t *proc, VALUE self,
	       int argc, const VALUE *argv, rb_block_t * blockptr)
{
    VALUE val = Qundef;
    int state;
    volatile int stored_safe = th->safe_level;
    rb_control_frame_t * volatile cfp = th->cfp;

    TH_PUSH_TAG(th);
    if ((state = EXEC_TAG()) == 0) {
	th->safe_level = proc->safe_level;
	val = invoke_block_from_c(th, &proc->block, self, argc, argv, blockptr, 0);
    }
    TH_POP_TAG();

    if (!proc->is_from_method) {
	th->safe_level = stored_safe;
    }

    if (state) {
	if (state == TAG_RETURN && proc->is_lambda) {
	    VALUE err = th->errinfo;
	    VALUE *escape_dfp = GET_THROWOBJ_CATCH_POINT(err);
	    VALUE *cdfp = proc->block.dfp;

	    if (escape_dfp == cdfp) {
		state = 0;
		th->errinfo = Qnil;
		th->cfp = cfp;
		val = GET_THROWOBJ_VAL(err);
	    }
	}
    }

    if (state) {
	JUMP_TAG(state);
    }
    return val;
}

/* special variable */

static VALUE
vm_cfp_svar_get(rb_thread_t *th, rb_control_frame_t *cfp, VALUE key)
{
    while (cfp->pc == 0) {
	cfp++;
    }
    return lfp_svar_get(th, cfp->lfp, key);
}

static void
vm_cfp_svar_set(rb_thread_t *th, rb_control_frame_t *cfp, VALUE key, const VALUE val)
{
    while (cfp->pc == 0) {
	cfp++;
    }
    lfp_svar_set(th, cfp->lfp, key, val);
}

static VALUE
vm_svar_get(VALUE key)
{
    rb_thread_t *th = GET_THREAD();
    return vm_cfp_svar_get(th, th->cfp, key);
}

static void
vm_svar_set(VALUE key, VALUE val)
{
    rb_thread_t *th = GET_THREAD();
    vm_cfp_svar_set(th, th->cfp, key, val);
}

VALUE
rb_backref_get(void)
{
    return vm_svar_get(1);
}

void
rb_backref_set(VALUE val)
{
    vm_svar_set(1, val);
}

VALUE
rb_lastline_get(void)
{
    return vm_svar_get(0);
}

void
rb_lastline_set(VALUE val)
{
    vm_svar_set(0, val);
}

/* backtrace */

int
vm_get_sourceline(const rb_control_frame_t *cfp)
{
    int line_no = 0;
    const rb_iseq_t *iseq = cfp->iseq;

    if (RUBY_VM_NORMAL_ISEQ_P(iseq)) {
	int i;
	int pos = cfp->pc - cfp->iseq->iseq_encoded;

	for (i = 0; i < iseq->insn_info_size; i++) {
	    if (iseq->insn_info_table[i].position == pos) {
		line_no = iseq->insn_info_table[i - 1].line_no;
		goto found;
	    }
	}
	line_no = iseq->insn_info_table[i - 1].line_no;
    }
  found:
    return line_no;
}

static VALUE
vm_backtrace_each(rb_thread_t *th,
		  const rb_control_frame_t *limit_cfp, const rb_control_frame_t *cfp,
		  const char * file, int line_no, VALUE ary)
{
    VALUE str;

    while (cfp > limit_cfp) {
	str = 0;
	if (cfp->iseq != 0) {
	    if (cfp->pc != 0) {
		rb_iseq_t *iseq = cfp->iseq;

		line_no = vm_get_sourceline(cfp);
		file = (char *)RSTRING_PTR(iseq->filename);
		str = rb_sprintf("%s:%d:in `%s'",
				 file, line_no, RSTRING_PTR(iseq->name));
		rb_ary_push(ary, str);
	    }
	}
	else if (RUBYVM_CFUNC_FRAME_P(cfp)) {
	    str = rb_sprintf("%s:%d:in `%s'",
			     file, line_no,
			     rb_id2name(cfp->method_id));
	    rb_ary_push(ary, str);
	}
	cfp = RUBY_VM_NEXT_CONTROL_FRAME(cfp);
    }
    return rb_ary_reverse(ary);
}

static inline VALUE
vm_backtrace(rb_thread_t *th, int lev)
{
    VALUE ary;
    const rb_control_frame_t *cfp = th->cfp;
    const rb_control_frame_t *top_of_cfp = (void *)(th->stack + th->stack_size);
    top_of_cfp -= 2;

    if (lev < 0) {
	/* TODO ?? */
	ary = rb_ary_new();
    }
    else {
	while (lev-- >= 0) {
	    cfp++;
	    if (cfp >= top_of_cfp) {
		return Qnil;
	    }
	}
	ary = rb_ary_new();
    }

    ary = vm_backtrace_each(th, RUBY_VM_NEXT_CONTROL_FRAME(cfp),
			    top_of_cfp, "", 0, ary);
    return ary;
}

const char *
rb_sourcefile(void)
{
    rb_thread_t *th = GET_THREAD();
    rb_control_frame_t *cfp = vm_get_ruby_level_cfp(th, th->cfp);

    if (cfp) {
	return RSTRING_PTR(cfp->iseq->filename);
    }
    else {
	return 0;
    }
}

int
rb_sourceline(void)
{
    rb_thread_t *th = GET_THREAD();
    rb_control_frame_t *cfp = vm_get_ruby_level_cfp(th, th->cfp);

    if (cfp) {
	return vm_get_sourceline(cfp);
    }
    else {
	return 0;
    }
}

NODE *
vm_cref(void)
{
    rb_thread_t *th = GET_THREAD();
    rb_control_frame_t *cfp = vm_get_ruby_level_cfp(th, th->cfp);
    return vm_get_cref(cfp->iseq, cfp->lfp, cfp->dfp);
}

#if 0
void
debug_cref(NODE *cref)
{
    while (cref) {
	dp(cref->nd_clss);
	printf("%ld\n", cref->nd_visi);
	cref = cref->nd_next;
    }
}
#endif

static NODE *
vm_cref_push(rb_thread_t *th, VALUE klass, int noex)
{
    NODE *cref = NEW_BLOCK(klass);
    rb_control_frame_t *cfp = vm_get_ruby_level_cfp(th, th->cfp);

    cref->nd_file = 0;
    cref->nd_next = vm_get_cref(cfp->iseq, cfp->lfp, cfp->dfp);
    cref->nd_visi = noex;
    return cref;
}

static inline VALUE
vm_get_cbase(const rb_iseq_t *iseq, const VALUE *lfp, const VALUE *dfp)
{
    NODE *cref = vm_get_cref(iseq, lfp, dfp);
    VALUE klass = Qundef;

    while (cref) {
	if ((klass = cref->nd_clss) != 0) {
	    break;
	}
	cref = cref->nd_next;
    }

    return klass;
}

VALUE
rb_vm_cbase(void)
{
    rb_thread_t *th = GET_THREAD();
    rb_control_frame_t *cfp = vm_get_ruby_level_cfp(th, th->cfp);
    return vm_get_cbase(cfp->iseq, cfp->lfp, cfp->dfp);
}

/* jump */

static VALUE
make_localjump_error(const char *mesg, VALUE value, int reason)
{
    extern VALUE rb_eLocalJumpError;
    VALUE exc = rb_exc_new2(rb_eLocalJumpError, mesg);
    ID id;

    switch (reason) {
      case TAG_BREAK:
	id = rb_intern("break");
	break;
      case TAG_REDO:
	id = rb_intern("redo");
	break;
      case TAG_RETRY:
	id = rb_intern("retry");
	break;
      case TAG_NEXT:
	id = rb_intern("next");
	break;
      case TAG_RETURN:
	id = rb_intern("return");
	break;
      default:
	id = rb_intern("noreason");
	break;
    }
    rb_iv_set(exc, "@exit_value", value);
    rb_iv_set(exc, "@reason", ID2SYM(id));
    return exc;
}

void
vm_localjump_error(const char *mesg, VALUE value, int reason)
{
    VALUE exc = make_localjump_error(mesg, value, reason);
    rb_exc_raise(exc);
}

VALUE
vm_make_jump_tag_but_local_jump(int state, VALUE val)
{
    VALUE result = Qnil;

    if (val == Qundef) {
	val = GET_THREAD()->tag->retval;
    }
    switch (state) {
      case 0:
	break;
      case TAG_RETURN:
	result = make_localjump_error("unexpected return", val, state);
	break;
      case TAG_BREAK:
	result = make_localjump_error("unexpected break", val, state);
	break;
      case TAG_NEXT:
	result = make_localjump_error("unexpected next", val, state);
	break;
      case TAG_REDO:
	result = make_localjump_error("unexpected redo", Qnil, state);
	break;
      case TAG_RETRY:
	result = make_localjump_error("retry outside of rescue clause", Qnil, state);
	break;
      default:
	break;
    }
    return result;
}

void
vm_jump_tag_but_local_jump(int state, VALUE val)
{
    VALUE exc = vm_make_jump_tag_but_local_jump(state, val);
    if (val != Qnil) {
	rb_exc_raise(exc);
    }
    JUMP_TAG(state);
}

NORETURN(static void vm_iter_break(rb_thread_t *th));

static void
vm_iter_break(rb_thread_t *th)
{
    rb_control_frame_t *cfp = th->cfp;
    VALUE *dfp = GC_GUARDED_PTR_REF(*cfp->dfp);

    th->state = TAG_BREAK;
    GC_WB(&th->errinfo, (VALUE)NEW_THROW_OBJECT(Qnil, (VALUE)dfp, TAG_BREAK));
    TH_JUMP_TAG(th, TAG_BREAK);
}

void
rb_iter_break(void)
{
    vm_iter_break(GET_THREAD());
}

/* optimization: redefine management */

VALUE ruby_vm_redefined_flag = 0;
static st_table *vm_opt_method_table = 0;

static void
rb_vm_check_redefinition_opt_method(const NODE *node)
{
#if !WITH_OBJC
    VALUE bop;

    if (vm_opt_method_table != NULL 
	&& st_lookup(vm_opt_method_table, (st_data_t)node, &bop)) {
	ruby_vm_redefined_flag |= bop;
    }
#endif
}

static void
add_opt_method(VALUE klass, ID mid, VALUE bop)
{
    NODE *node;
#if WITH_OBJC
    if ((node = rb_method_node(klass, mid)) != NULL) {
#else
    if (st_lookup(RCLASS_M_TBL(klass), mid, (void *)&node) &&
	nd_type(node->nd_body->nd_body) == NODE_CFUNC) {
#endif
	st_insert(vm_opt_method_table, (st_data_t)node, (st_data_t)bop);
    }
    else {
	rb_bug("undefined optimized method: %s", rb_id2name(mid));
    }
}

static void
vm_init_redefined_flag(void)
{
    ID mid;
    VALUE bop;

    vm_opt_method_table = st_init_numtable();
    GC_ROOT(&vm_opt_method_table);

#define OP(mid_, bop_) (mid = id##mid_, bop = BOP_##bop_)
#define C(k) add_opt_method(rb_c##k, mid, bop)
    OP(PLUS, PLUS), (C(Fixnum), C(Float), C(String), C(Array));
    OP(MINUS, MINUS), (C(Fixnum));
    OP(MULT, MULT), (C(Fixnum), C(Float));
    OP(DIV, DIV), (C(Fixnum), C(Float));
    OP(MOD, MOD), (C(Fixnum), C(Float));
    OP(Eq, EQ), (C(Fixnum), C(Float), C(String));
    OP(LT, LT), (C(Fixnum));
    OP(LE, LE), (C(Fixnum));
    OP(LTLT, LTLT), (C(String), C(Array));
    OP(AREF, AREF), (C(Array), C(Hash));
    OP(ASET, ASET), (C(Array), C(Hash));
#if !WITH_OBJC
    OP(Length, LENGTH), (C(Array), C(String), C(Hash));
#endif
    OP(Succ, SUCC), (C(Fixnum), C(String), C(Time));
    OP(GT, GT), (C(Fixnum));
    OP(GE, GE), (C(Fixnum));
#undef C
#undef OP
}

/* evaluator body */

#include "vm_evalbody.c"

/*                  finish
  VMe (h1)          finish
    VM              finish F1 F2
      cfunc         finish F1 F2 C1
        rb_funcall  finish F1 F2 C1
          VMe       finish F1 F2 C1
            VM      finish F1 F2 C1 F3

  F1 - F3 : pushed by VM
  C1      : pushed by send insn (CFUNC)

  struct CONTROL_FRAME {
    VALUE *pc;                  // cfp[0], program counter
    VALUE *sp;                  // cfp[1], stack pointer
    VALUE *bp;                  // cfp[2], base pointer
    rb_iseq_t *iseq;            // cfp[3], iseq
    VALUE flag;                 // cfp[4], magic
    VALUE self;                 // cfp[5], self
    VALUE *lfp;                 // cfp[6], local frame pointer
    VALUE *dfp;                 // cfp[7], dynamic frame pointer
    rb_iseq_t * block_iseq;     // cfp[8], block iseq
    VALUE proc;                 // cfp[9], always 0
  };

  struct BLOCK {
    VALUE self;
    VALUE *lfp;
    VALUE *dfp;
    rb_iseq_t *block_iseq;
    VALUE proc;
  };

  struct METHOD_CONTROL_FRAME {
    rb_control_frame_t frame;
  };

  struct METHOD_FRAME {
    VALUE arg0;
    ...
    VALUE argM;
    VALUE param0;
    ...
    VALUE paramN;
    VALUE cref;
    VALUE special;                         // lfp [1]
    struct block_object *block_ptr | 0x01; // lfp [0]
  };

  struct BLOCK_CONTROL_FRAME {
    rb_control_frame_t frame;
  };

  struct BLOCK_FRAME {
    VALUE arg0;
    ...
    VALUE argM;
    VALUE param0;
    ...
    VALUE paramN;
    VALUE cref;
    VALUE *(prev_ptr | 0x01); // DFP[0]
  };

  struct CLASS_CONTROL_FRAME {
    rb_control_frame_t frame;
  };

  struct CLASS_FRAME {
    VALUE param0;
    ...
    VALUE paramN;
    VALUE cref;
    VALUE prev_dfp; // for frame jump
  };

  struct C_METHOD_CONTROL_FRAME {
    VALUE *pc;                       // 0
    VALUE *sp;                       // stack pointer
    VALUE *bp;                       // base pointer (used in exception)
    rb_iseq_t *iseq;               // cmi
    VALUE magic;                     // C_METHOD_FRAME
    VALUE self;                      // ?
    VALUE *lfp;                      // lfp
    VALUE *dfp;                      // == lfp
    rb_iseq_t * block_iseq;        //
    VALUE proc;                      // always 0
  };

  struct C_BLOCK_CONTROL_FRAME {
    VALUE *pc;                       // point only "finish" insn
    VALUE *sp;                       // sp
    rb_iseq_t *iseq;               // ?
    VALUE magic;                     // C_METHOD_FRAME
    VALUE self;                      // needed?
    VALUE *lfp;                      // lfp
    VALUE *dfp;                      // lfp
    rb_iseq_t * block_iseq; // 0
  };
 */


static VALUE
vm_eval_body(rb_thread_t *th)
{
    int state;
    VALUE result, err;
    VALUE initial = 0;
    VALUE *escape_dfp = NULL;

    TH_PUSH_TAG(th);
    _tag.retval = Qnil;
    if ((state = EXEC_TAG()) == 0) {
      vm_loop_start:
	result = vm_eval(th, initial);
	if ((state = th->state) != 0) {
	    err = result;
	    th->state = 0;
	    goto exception_handler;
	}
    }
    else {
	int i;
	struct iseq_catch_table_entry *entry;
	unsigned long epc, cont_pc, cont_sp;
	VALUE catch_iseqval;
	rb_control_frame_t *cfp;
	VALUE type;

	err = th->errinfo;

	if (state == TAG_RAISE) {
	    rb_ivar_set(err, idThrowState, INT2FIX(state));
	}

      exception_handler:
	cont_pc = cont_sp = catch_iseqval = 0;

	while (th->cfp->pc == 0 || th->cfp->iseq == 0) {
	    th->cfp++;
	}

	cfp = th->cfp;
	epc = cfp->pc - cfp->iseq->iseq_encoded;

	if (state == TAG_BREAK || state == TAG_RETURN) {
	    escape_dfp = GET_THROWOBJ_CATCH_POINT(err);

	    if (cfp->dfp == escape_dfp) {
		if (state == TAG_RETURN) {
		    if ((cfp + 1)->pc != &finish_insn_seq[0]) {
			SET_THROWOBJ_CATCH_POINT(err, (VALUE)(cfp + 1)->dfp);
			SET_THROWOBJ_STATE(err, state = TAG_BREAK);
		    }
		    else {
			result = GET_THROWOBJ_VAL(err);
			th->errinfo = Qnil;
			th->cfp += 2;
			goto finish_vme;
		    }
		    /* through */
		}
		else {
		    /* TAG_BREAK */
#if OPT_STACK_CACHING
		    initial = (GET_THROWOBJ_VAL(err));
#else
		    *th->cfp->sp++ = (GET_THROWOBJ_VAL(err));
#endif
		    th->errinfo = Qnil;
		    goto vm_loop_start;
		}
	    }
	}

	if (state == TAG_RAISE) {
	    for (i = 0; i < cfp->iseq->catch_table_size; i++) {
		entry = &cfp->iseq->catch_table[i];
		if (entry->start < epc && entry->end >= epc) {

		    if (entry->type == CATCH_TYPE_RESCUE ||
			entry->type == CATCH_TYPE_ENSURE) {
			catch_iseqval = entry->iseq;
			cont_pc = entry->cont;
			cont_sp = entry->sp;
			break;
		    }
		}
	    }
	}
	else if (state == TAG_RETRY) {
	    for (i = 0; i < cfp->iseq->catch_table_size; i++) {
		entry = &cfp->iseq->catch_table[i];
		if (entry->start < epc && entry->end >= epc) {

		    if (entry->type == CATCH_TYPE_ENSURE) {
			catch_iseqval = entry->iseq;
			cont_pc = entry->cont;
			cont_sp = entry->sp;
			break;
		    }
		    else if (entry->type == CATCH_TYPE_RETRY) {
			VALUE *escape_dfp;
			escape_dfp = GET_THROWOBJ_CATCH_POINT(err);
			if (cfp->dfp == escape_dfp) {
			    cfp->pc = cfp->iseq->iseq_encoded + entry->cont;
			    th->errinfo = Qnil;
			    goto vm_loop_start;
			}
		    }
		}
	    }
	}
	else if (state == TAG_BREAK && ((VALUE)escape_dfp & ~0x03) == 0) {
	    type = CATCH_TYPE_BREAK;

	  search_restart_point:
	    for (i = 0; i < cfp->iseq->catch_table_size; i++) {
		entry = &cfp->iseq->catch_table[i];

		if (entry->start < epc && entry->end >= epc) {
		    if (entry->type == CATCH_TYPE_ENSURE) {
			catch_iseqval = entry->iseq;
			cont_pc = entry->cont;
			cont_sp = entry->sp;
			break;
		    }
		    else if (entry->type == type) {
			cfp->pc = cfp->iseq->iseq_encoded + entry->cont;
			cfp->sp = cfp->bp + entry->sp;

			if (state != TAG_REDO) {
#if OPT_STACK_CACHING
			    initial = (GET_THROWOBJ_VAL(err));
#else
			    *th->cfp->sp++ = (GET_THROWOBJ_VAL(err));
#endif
			}
			th->errinfo = Qnil;
			goto vm_loop_start;
		    }
		}
	    }
	}
	else if (state == TAG_REDO) {
	    type = CATCH_TYPE_REDO;
	    goto search_restart_point;
	}
	else if (state == TAG_NEXT) {
	    type = CATCH_TYPE_NEXT;
	    goto search_restart_point;
	}
	else {
	    for (i = 0; i < cfp->iseq->catch_table_size; i++) {
		entry = &cfp->iseq->catch_table[i];
		if (entry->start < epc && entry->end >= epc) {

		    if (entry->type == CATCH_TYPE_ENSURE) {
			catch_iseqval = entry->iseq;
			cont_pc = entry->cont;
			cont_sp = entry->sp;
			break;
		    }
		}
	    }
	}

	if (catch_iseqval != 0) {
	    /* found catch table */
	    rb_iseq_t *catch_iseq;

	    /* enter catch scope */
	    GetISeqPtr(catch_iseqval, catch_iseq);
	    cfp->sp = cfp->bp + cont_sp;
	    cfp->pc = cfp->iseq->iseq_encoded + cont_pc;

	    /* push block frame */
	    cfp->sp[0] = err;
	    vm_push_frame(th, catch_iseq, FRAME_MAGIC_BLOCK,
			  cfp->self, (VALUE)cfp->dfp, catch_iseq->iseq_encoded,
			  cfp->sp + 1 /* push value */, cfp->lfp, catch_iseq->local_size - 1);

	    state = 0;
	    th->errinfo = Qnil;
	    goto vm_loop_start;
	}
	else {
	    th->cfp++;
	    if (th->cfp->pc != &finish_insn_seq[0]) {
		goto exception_handler;
	    }
	    else {
		vm_pop_frame(th);
		GC_WB(&th->errinfo, err);
		TH_POP_TAG2();
		JUMP_TAG(state);
	    }
	}
    }
  finish_vme:
    TH_POP_TAG();
    return result;
}

/* misc */

VALUE
rb_iseq_eval(VALUE iseqval)
{
    rb_thread_t *th = GET_THREAD();
    VALUE val;
    volatile VALUE tmp;

    vm_set_top_stack(th, iseqval);

    if (!rb_const_defined(rb_cObject, rb_intern("TOPLEVEL_BINDING"))) {
	rb_define_global_const("TOPLEVEL_BINDING", rb_binding_new());
    }
    val = vm_eval_body(th);
    tmp = iseqval; /* prohibit tail call optimization */
    return val;
}

int
rb_thread_method_id_and_class(rb_thread_t *th,
			      ID *idp, VALUE *klassp)
{
    rb_control_frame_t *cfp = th->cfp;
    rb_iseq_t *iseq = cfp->iseq;
    if (!iseq) {
	if (idp) *idp = cfp->method_id;
	if (klassp) *klassp = cfp->method_class;
	return 1;
    }
    while (iseq) {
	if (RUBY_VM_IFUNC_P(iseq)) {
	    if (idp) *idp = rb_intern("<ifunc>");
	    if (klassp) *klassp = 0;
	    return 1;
	}
	if (iseq->defined_method_id) {
	    if (idp) *idp = iseq->defined_method_id;
	    if (klassp) *klassp = iseq->klass;
	    return 1;
	}
	if (iseq->local_iseq == iseq) {
	    break;
	}
	iseq = iseq->parent_iseq;
    }
    return 0;
}

int
rb_frame_method_id_and_class(ID *idp, VALUE *klassp)
{
    return rb_thread_method_id_and_class(GET_THREAD(), idp, klassp);
}

VALUE
rb_thread_current_status(const rb_thread_t *th)
{
    const rb_control_frame_t *cfp = th->cfp;
    VALUE str = Qnil;

    if (cfp->iseq != 0) {
	if (cfp->pc != 0) {
	    rb_iseq_t *iseq = cfp->iseq;
	    int line_no = vm_get_sourceline(cfp);
	    const char *file = RSTRING_PTR(iseq->filename);
	    str = rb_sprintf("%s:%d:in `%s'",
			     file, line_no, RSTRING_PTR(iseq->name));
	}
    }
    else if (cfp->method_id) {
	str = rb_sprintf("`%s#%s' (cfunc)",
			 RSTRING_PTR(rb_class_name(cfp->method_class)),
			 rb_id2name(cfp->method_id));
    }

    return str;
}

VALUE
rb_vm_call_cfunc(VALUE recv, VALUE (*func)(VALUE), VALUE arg,
		 const rb_block_t *blockptr, VALUE filename)
{
    rb_thread_t *th = GET_THREAD();
    const rb_control_frame_t *reg_cfp = th->cfp;
    volatile VALUE iseqval = rb_iseq_new(0, filename, filename, 0, ISEQ_TYPE_TOP);
    VALUE val;

    vm_push_frame(th, DATA_PTR(iseqval), FRAME_MAGIC_TOP,
		  recv, (VALUE)blockptr, 0, reg_cfp->sp, 0, 1);

    val = (*func)(arg);

    vm_pop_frame(th);
    return val;
}

int
rb_vm_cfunc_funcall_p(const rb_control_frame_t *cfp)
{
    if (vm_cfunc_flags(cfp) & (VM_CALL_FCALL_BIT | VM_CALL_VCALL_BIT))
	return Qtrue;
    return Qfalse;
}

/* vm */

static void
vm_free(void *ptr)
{
    RUBY_FREE_ENTER("vm");
    if (ptr) {
	rb_vm_t *vmobj = ptr;

	st_free_table(vmobj->living_threads);
	vmobj->living_threads = 0;
	/* TODO: MultiVM Instance */
	/* VM object should not be cleaned by GC */
	/* ruby_xfree(ptr); */
	/* ruby_current_vm = 0; */
    }
    RUBY_FREE_LEAVE("vm");
}

static int
vm_mark_each_thread_func(st_data_t key, st_data_t value, st_data_t dummy)
{
#if !WITH_OBJC
    VALUE thval = (VALUE)key;
#endif
    rb_gc_mark(thval);
    return ST_CONTINUE;
}

static void
mark_event_hooks(rb_event_hook_t *hook)
{
    while (hook) {
	rb_gc_mark(hook->data);
	hook = hook->next;
    }
}

void
rb_vm_mark(void *ptr)
{
    RUBY_MARK_ENTER("vm");
    RUBY_GC_INFO("-------------------------------------------------\n");
    if (ptr) {
	rb_vm_t *vm = ptr;
	if (vm->living_threads) {
	    st_foreach(vm->living_threads, vm_mark_each_thread_func, 0);
	}
	RUBY_MARK_UNLESS_NULL(vm->thgroup_default);
	RUBY_MARK_UNLESS_NULL(vm->mark_object_ary);
	RUBY_MARK_UNLESS_NULL(vm->last_status);
	RUBY_MARK_UNLESS_NULL(vm->load_path);
	RUBY_MARK_UNLESS_NULL(vm->loaded_features);
	RUBY_MARK_UNLESS_NULL(vm->top_self);

	if (vm->loading_table) {
	    rb_mark_tbl(vm->loading_table);
	}

	mark_event_hooks(vm->event_hooks);
    }

    RUBY_MARK_LEAVE("vm");
}

static void
vm_init2(rb_vm_t *vm)
{
    MEMZERO(vm, rb_vm_t, 1);
}

/* Thread */

#define USE_THREAD_DATA_RECYCLE 1

#if USE_THREAD_DATA_RECYCLE
#define RECYCLE_MAX 64
#if WITH_OBJC
VALUE *thread_recycle_stack_slot;
#else
VALUE *thread_recycle_stack_slot[RECYCLE_MAX];
#endif
int thread_recycle_stack_count = 0;

static VALUE *
thread_recycle_stack(int size)
{
    if (thread_recycle_stack_count) {
	return (VALUE *)thread_recycle_stack_slot[--thread_recycle_stack_count];
    }
    else {
	return ALLOC_N(VALUE, size);
    }
}

#else
#define thread_recycle_stack(size) ALLOC_N(VALUE, (size))
#endif

void
rb_thread_recycle_stack_release(VALUE *stack)
{
#if USE_THREAD_DATA_RECYCLE
    if (thread_recycle_stack_count < RECYCLE_MAX) {
	GC_WB(&thread_recycle_stack_slot[thread_recycle_stack_count++], stack);
    }
    else {
	ruby_xfree(stack);
    }
#else
	ruby_xfree(stack);
#endif
}

#ifdef USE_THREAD_RECYCLE
static rb_thread_t *
thread_recycle_struct(void)
{
    void *p = ALLOC_N(rb_thread_t, 1);
    memset(p, 0, sizeof(rb_thread_t));
    return p;
}
#endif

static void
thread_free(void *ptr)
{
    rb_thread_t *th;
    RUBY_FREE_ENTER("thread");

    if (ptr) {
	th = ptr;
	
	if (!th->root_fiber) {
	    RUBY_FREE_UNLESS_NULL(th->stack);
	}

	if (th->local_storage) {
	    st_free_table(th->local_storage);
	}

#if USE_VALUE_CACHE
	{
	    VALUE *ptr = th->value_cache_ptr;
	    while (*ptr) {
		VALUE v = *ptr;
		RBASIC(v)->flags = 0;
		RBASIC(v)->klass = 0;
		ptr++;
	    }
	}
#endif

	if (th->vm->main_thread == th) {
	    RUBY_GC_INFO("main thread\n");
	}
	else {
	    ruby_xfree(ptr);
	}
    }
    RUBY_FREE_LEAVE("thread");
}

#if !WITH_OBJC
void rb_gc_mark_machine_stack(rb_thread_t *th);

void
rb_thread_mark(void *ptr)
{
    rb_thread_t *th = NULL;
    RUBY_MARK_ENTER("thread");
    if (ptr) {
	th = ptr;
	if (th->stack) {
	    VALUE *p = th->stack;
	    VALUE *sp = th->cfp->sp;
	    rb_control_frame_t *cfp = th->cfp;
	    rb_control_frame_t *limit_cfp = (void *)(th->stack + th->stack_size);

	    while (p < sp) {
		rb_gc_mark(*p++);
	    }
	    rb_gc_mark_locations(p, p + th->mark_stack_len);

	    while (cfp != limit_cfp) {
		rb_gc_mark(cfp->proc);
		cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
	    }
	}

	/* mark ruby objects */
	RUBY_MARK_UNLESS_NULL(th->first_proc);
	if (th->first_proc) RUBY_MARK_UNLESS_NULL(th->first_args);

	RUBY_MARK_UNLESS_NULL(th->thgroup);
	RUBY_MARK_UNLESS_NULL(th->value);
	RUBY_MARK_UNLESS_NULL(th->errinfo);
	RUBY_MARK_UNLESS_NULL(th->thrown_errinfo);
	RUBY_MARK_UNLESS_NULL(th->local_svar);
	RUBY_MARK_UNLESS_NULL(th->top_self);
	RUBY_MARK_UNLESS_NULL(th->top_wrapper);
	RUBY_MARK_UNLESS_NULL(th->fiber);
	RUBY_MARK_UNLESS_NULL(th->root_fiber);
	RUBY_MARK_UNLESS_NULL(th->stat_insn_usage);

	rb_mark_tbl(th->local_storage);

	if (GET_THREAD() != th && th->machine_stack_start && th->machine_stack_end) {
	    rb_gc_mark_machine_stack(th);
	    rb_gc_mark_locations((VALUE *)&th->machine_regs,
				 (VALUE *)(&th->machine_regs) +
				 sizeof(th->machine_regs) / sizeof(VALUE));
	}

	mark_event_hooks(th->event_hooks);
    }

    RUBY_MARK_LEAVE("thread");
}
#else
# define rb_thread_mark (NULL)
#endif

static VALUE
thread_alloc(VALUE klass)
{
    VALUE volatile obj;
#ifdef USE_THREAD_RECYCLE
    rb_thread_t *th = thread_recycle_struct();
    obj = Data_Wrap_Struct(klass, rb_thread_mark, thread_free, th);
#else
    rb_thread_t *th;
    obj = Data_Make_Struct(klass, rb_thread_t,
			   rb_thread_mark, thread_free, th);
#endif
    return obj;
}

static void
th_init2(rb_thread_t *th)
{
    /* allocate thread stack */
    th->stack_size = RUBY_VM_THREAD_STACK_SIZE;
    GC_WB(&th->stack, thread_recycle_stack(th->stack_size));

    th->cfp = (void *)(th->stack + th->stack_size);

    vm_push_frame(th, 0, FRAME_MAGIC_TOP, Qnil, 0, 0,
		  th->stack, 0, 1);

    th->status = THREAD_RUNNABLE;
    th->errinfo = Qnil;

#if USE_VALUE_CACHE
    th->value_cache_ptr = &th->value_cache[0];
#endif
}

static void
th_init(rb_thread_t *th)
{
    th_init2(th);
}

static VALUE
ruby_thread_init(VALUE self)
{
    rb_thread_t *th;
    rb_vm_t *vm = GET_THREAD()->vm;
    GetThreadPtr(self, th);

    th_init(th);
    th->self = self;
    th->vm = vm;

    th->top_wrapper = 0;
    th->top_self = rb_vm_top_self();
    return self;
}

VALUE
rb_thread_alloc(VALUE klass)
{
    VALUE self = thread_alloc(klass);
    ruby_thread_init(self);
    return self;
}

VALUE insns_name_array(void);
extern VALUE *rb_gc_stack_start;
extern size_t rb_gc_stack_maxsize;
#ifdef __ia64
extern VALUE *rb_gc_register_stack_start;
#endif

/* debug functions */

static VALUE
sdr(void)
{
    rb_vm_bugreport();
    return Qnil;
}

static VALUE
nsdr(void)
{
    VALUE ary = rb_ary_new();
#if HAVE_BACKTRACE
#include <execinfo.h>
#define MAX_NATIVE_TRACE 1024
    static void *trace[MAX_NATIVE_TRACE];
    int n = backtrace(trace, MAX_NATIVE_TRACE);
    char **syms = backtrace_symbols(trace, n);
    int i;

    if (syms == 0) {
	rb_memerror();
    }

    for (i=0; i<n; i++) {
	rb_ary_push(ary, rb_str_new2(syms[i]));
    }
    free(syms);
#endif
    return ary;
}

void
Init_VM(void)
{
    VALUE opts;

    /* ::VM */
    rb_cVM = rb_define_class("VM", rb_cObject);
    rb_undef_alloc_func(rb_cVM);

    /* Env */
    rb_cEnv = rb_define_class_under(rb_cVM, "Env", rb_cObject);
    rb_undef_alloc_func(rb_cEnv);

    /* ::Thread */
    rb_cThread = rb_define_class("Thread", rb_cObject);
    rb_undef_alloc_func(rb_cThread);

    /* ::VM::USAGE_ANALYSIS_* */
    rb_define_const(rb_cVM, "USAGE_ANALYSIS_INSN", rb_hash_new());
    rb_define_const(rb_cVM, "USAGE_ANALYSIS_REGS", rb_hash_new());
    rb_define_const(rb_cVM, "USAGE_ANALYSIS_INSN_BIGRAM", rb_hash_new());
    rb_define_const(rb_cVM, "OPTS", opts = rb_ary_new());

#if   OPT_DIRECT_THREADED_CODE
    rb_ary_push(opts, rb_str_new2("direct threaded code"));
#elif OPT_TOKEN_THREADED_CODE
    rb_ary_push(opts, rb_str_new2("token threaded code"));
#elif OPT_CALL_THREADED_CODE
    rb_ary_push(opts, rb_str_new2("call threaded code"));
#endif

#if OPT_BASIC_OPERATIONS
    rb_ary_push(opts, rb_str_new2("optimize basic operation"));
#endif

#if OPT_STACK_CACHING
    rb_ary_push(opts, rb_str_new2("stack caching"));
#endif
#if OPT_OPERANDS_UNIFICATION
    rb_ary_push(opts, rb_str_new2("operands unification]"));
#endif
#if OPT_INSTRUCTIONS_UNIFICATION
    rb_ary_push(opts, rb_str_new2("instructions unification"));
#endif
#if OPT_INLINE_METHOD_CACHE
    rb_ary_push(opts, rb_str_new2("inline method cache"));
#endif
#if OPT_BLOCKINLINING
    rb_ary_push(opts, rb_str_new2("block inlining"));
#endif

    /* ::VM::InsnNameArray */
    rb_define_const(rb_cVM, "INSTRUCTION_NAMES", insns_name_array());

    /* debug functions ::VM::SDR(), ::VM::NSDR() */
#if VMDEBUG
    rb_define_singleton_method(rb_cVM, "SDR", sdr, 0);
    rb_define_singleton_method(rb_cVM, "NSDR", nsdr, 0);
#else
    (void)sdr;
    (void)nsdr;
#endif

    /* VM bootstrap: phase 2 */
    {
	rb_vm_t *vm = ruby_current_vm;
	rb_thread_t *th = GET_THREAD();
	VALUE filename = rb_str_new2("<dummy toplevel>");
	volatile VALUE iseqval = rb_iseq_new(0, filename, filename, 0, ISEQ_TYPE_TOP);
        volatile VALUE th_self;
	rb_iseq_t *iseq;

	/* create vm object */
	GC_WB(&vm->self, Data_Wrap_Struct(rb_cVM, rb_vm_mark, vm_free, vm));

	/* create main thread */
	th_self = Data_Wrap_Struct(rb_cThread, rb_thread_mark,
					      thread_free, th);
	GC_WB(&th->self, th_self);
	vm->main_thread = th;
	vm->running_thread = th;
	th->vm = vm;
	th->top_wrapper = 0;
	th->top_self = rb_vm_top_self();
	rb_thread_set_current(th);

	GC_WB(&vm->living_threads, st_init_numtable());
	st_insert(vm->living_threads, th_self, (st_data_t) th->thread_id);

	rb_register_mark_object(iseqval);
	rb_objc_retain((void *)iseqval);	
	GetISeqPtr(iseqval, iseq);
	th->cfp->iseq = iseq;
	th->cfp->pc = iseq->iseq_encoded;
    }
    vm_init_redefined_flag();
}

struct rb_objspace *rb_objspace_alloc(void);

void
Init_BareVM(void)
{
    /* VM bootstrap: phase 1 */
#if WITH_OBJC
    rb_vm_t *vm = xmalloc(sizeof(*vm));
    rb_thread_t *th = xmalloc(sizeof(*th));
#else
    rb_vm_t *vm = malloc(sizeof(*vm));
    rb_thread_t *th = malloc(sizeof(*th));
#endif
    MEMZERO(th, rb_thread_t, 1);

    rb_thread_set_current_raw(th);
    GC_ROOT(&ruby_current_thread);

#if USE_THREAD_DATA_RECYCLE
# if WITH_OBJC
    thread_recycle_stack_slot = xmalloc(sizeof(VALUE) * RECYCLE_MAX);
    GC_ROOT(&thread_recycle_stack_slot);
# endif
#endif

    vm_init2(vm);
#if defined(ENABLE_VM_OBJSPACE) && ENABLE_VM_OBJSPACE
    vm->objspace = rb_objspace_alloc();
#endif
    ruby_current_vm = vm;
    GC_ROOT(&ruby_current_vm);

    th_init2(th);
    th->vm = vm;
    th->machine_stack_start = rb_gc_stack_start;
    th->machine_stack_maxsize = rb_gc_stack_maxsize;
#ifdef __ia64
    th->machine_register_stack_start = rb_gc_register_stack_start;
    th->machine_stack_maxsize /= 2;
    th->machine_register_stack_maxsize = th->machine_stack_maxsize;
#endif
}

/* top self */

static VALUE
main_to_s(VALUE obj)
{
    return rb_str_new2("main");
}

VALUE
rb_vm_top_self(void)
{
    return GET_VM()->top_self;
}

void
Init_top_self(void)
{
    rb_vm_t *vm = GET_VM();

    GC_WB(&vm->top_self, rb_obj_alloc(rb_cObject));
    rb_define_singleton_method(rb_vm_top_self(), "to_s", main_to_s, 0);
}
