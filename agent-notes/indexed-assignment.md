# Indexed assignment

## Goal

Fix ToastStunt indexed and range assignment so it preserves writes made to the same variable or property while evaluating the target or the right-hand side.

## State

The expanded cases reproduced on the upstream baseline and returned the expected values on the rebuilt patch. The index/range and map suites pass on disposable servers, including an assignment that crosses a suspension and a path invalidated by the RHS. Mongoose #799 records the broader reproduction and remains open pending rollout. The upstream report is [ToastStunt #112](https://github.com/lisdude/toaststunt/issues/112).

## Decisions and why

Keep the existing evaluation order and use the saved indices and bounds. Refresh the containers before writing so side effects survive without reevaluating the expressions that produced those indices and bounds. The compiler emits the refresh where the target or RHS could affect the root.

## Surprises

The nested write path detaches the child from a private copy of the parent as a copy-on-write optimization. A refresh must account for those detached slots.

## Next step

Submit the patch against upstream master and check the pull request's status. Mongoose deployment is separate work.
