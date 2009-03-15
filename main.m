#undef RUBY_EXPORT
#include "ruby.h"
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

RUBY_GLOBAL_SETUP

#define POOL_DO(POOL)   { id POOL = [[NSAutoreleasePool alloc] init];
#define END_POOL(POOL)  [(POOL) release]; }

static char* resource_path()
{
	char* result;
	POOL_DO(pool) {
		result = strdup([[[NSBundle mainBundle] resourcePath] fileSystemRepresentation]);
	} END_POOL(pool);
	return result;
}

#define ARGS 5
int
main(int argc, char **argv, char **envp)
{
	int c = ARGS;
	char *v[ARGS];
	char **vv = v;
	v[0] = "macaruby";
	v[1] = "-I";
	v[2] = resource_path();
	v[3] = "-e";
	v[4] = "require 'main'";
	
#ifdef RUBY_DEBUG_ENV
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#endif
	
    ruby_sysinit(&c, &vv);
    {
		RUBY_INIT_STACK;
		ruby_init();
		return ruby_run_node(ruby_options(c, v));
    }
}
