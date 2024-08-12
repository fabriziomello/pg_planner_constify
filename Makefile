MODULES = pg_planner_constify

REGRESS = pg_planner_constify
REGRESS_OPTS = --temp-instance=tmp_check --temp-config=./pg_planner_constify.conf

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

format:
	find . -regex '.*\.\(c\|h\)' | xargs clang-format -i

.PHONY: format
