#include <postgres.h>

#include <string.h>

#include <fmgr.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/optimizer.h>
#include <optimizer/planner.h>
#include <storage/ipc.h>
#include <utils/fmgrprotos.h>
#include <utils/guc.h>
#include <utils/typcache.h>

PG_MODULE_MAGIC;

static planner_hook_type prev_planner_hook;
static char *guc_function = NULL;
static Oid guc_function_oid = InvalidOid;

static bool
is_valid_function(FuncExpr *funcExpr)
{
	return (OidIsValid(guc_function_oid) && funcExpr->funcid == guc_function_oid &&
			!funcExpr->funcretset);
}

static Node *
constify_function_call(Node *node)
{
	Assert(node);

	switch (nodeTag(node))
	{
		case T_OpExpr:
		{
			OpExpr *op = copyObject(castNode(OpExpr, node));

			if (IsA(lsecond(op->args), FuncExpr))
			{
				FuncExpr *funcExpr = castNode(FuncExpr, lsecond(op->args));

				if (is_valid_function(funcExpr))
				{
					Datum result;
					Const *const_value;
					TypeCacheEntry *tce = lookup_type_cache(funcExpr->funcresulttype, 0);

					elog(DEBUG1, "constifying function %s", guc_function);

					result = OidFunctionCall0(funcExpr->funcid);

					const_value = makeConst(/* consttype */ funcExpr->funcresulttype,
											/* consttypmod */ -1,
											/* constcollid */ funcExpr->funccollid,
											/* constlen */ tce->typlen,
											/* constvalue */ result,
											/* constisnull */ false,
											/* constbyval */ false);

					op->args = list_make2(linitial(op->args), (Node *) const_value);
					return (Node *) op;
				}
			}
			break;
		}
		case T_BoolExpr:
		{
			List *constified = NIL;
			BoolExpr *be = castNode(BoolExpr, node);
			ListCell *lc;

			foreach (lc, be->args)
			{
				constified = lappend(constified, constify_function_call((Node *) lfirst(lc)));
			}

			if (constified)
				be->args = constified;

			break;
		}
		default:
			break;
	}

	return node;
}

static bool
planner_constify_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;

	if (guc_function == NULL)
		return false;

	switch (nodeTag(node))
	{
		case T_FromExpr:
		{
			FromExpr *from = castNode(FromExpr, node);
			if (from->quals)
			{
				elog(DEBUG1, "constifying from clause");
				from->quals = constify_function_call(from->quals);
			}
			break;
		}

		case T_Query:
			return query_tree_walker(castNode(Query, node), planner_constify_walker, context, 0);
			break;

		default:
			break;
	}

	return expression_tree_walker(node, planner_constify_walker, context);
}

static PlannedStmt *
planner_constify(Query *parse, const char *query_string, int cursor_opts,
				 ParamListInfo bound_params)
{
	PlannedStmt *stmt;

	elog(DEBUG1, "entering planner_constify");

	if (guc_function)
	{
		if (!OidIsValid(guc_function_oid))
			guc_function_oid = DatumGetObjectId(
				DirectFunctionCall1(regprocedurein, CStringGetDatum(guc_function)));

		if (OidIsValid(guc_function_oid))
			planner_constify_walker((Node *) parse, NULL);
	}

	if (prev_planner_hook != NULL)
		/* Call any earlier hooks */
		stmt = (prev_planner_hook) (parse, query_string, cursor_opts, bound_params);
	else
		/* Call the standard planner */
		stmt = standard_planner(parse, query_string, cursor_opts, bound_params);

	elog(DEBUG1, "leaving planner_constify");

	return stmt;
}

/* Called when the backend exits */
static void
on_backend_exit(int code, Datum arg)
{
	planner_hook = prev_planner_hook;
}

static void
guc_function_assign_hook(const char *newval, void *extra)
{
	if (newval == NULL || guc_function == NULL || strcmp(guc_function, newval) != 0)
		guc_function_oid = InvalidOid;
}

void
_PG_init(void)
{
	prev_planner_hook = planner_hook;
	planner_hook = planner_constify;

	DefineCustomStringVariable(/* name= */ "pg_planner_constify.function",
							   /* short_desc= */ "A variable for testing pg_planner_constify",
							   /* long_desc= */ NULL,
							   /* valueAddr= */ &guc_function,
							   /* Value= */ NULL,
							   /* context= */ PGC_USERSET,
							   /* flags= */ 0,
							   /* check_hook= */ NULL,
							   /* assign_hook= */ guc_function_assign_hook,
							   /* show_hook= */ NULL);

	/* Register a function to be called when the backend exits */
	on_proc_exit(on_backend_exit, 0);
}
