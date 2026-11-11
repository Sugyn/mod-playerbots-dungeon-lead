# Route table updates

`sql/playerbots_dungeon_route.sql` is the complete route table: a fresh install loads only that
file. It always contains the result of every update below.

An existing install applies the update files it does not have yet, in name order. Each change to
the route table ships as a numbered pair:

| File | Content |
|---|---|
| `NNNN_<topic>.sql` | the change (usually: replace one dungeon's route) |
| `NNNN_<topic>.rollback.sql` | restores the rows exactly as they were before `NNNN_<topic>.sql` |

- `NNNN` is the next free four-digit number (`0001`, `0002`, ...); never renumber a published file.
- The same change goes into `data/dungeon_routes.csv`, `data/routes.tsv` and the base SQL in the
  same pull request. `tools/route_insert.py` writes all of them, including both update files.
- `python3 tools/check_route_sql.py` must pass (CSV == base SQL). With database access,
  `--upgrade-from <rev> --mysql "<cmd>"` also proves that the base SQL of `<rev>` plus the new
  updates equals a fresh install; rollback files are not part of that path.
- The worldserver reads the table at startup only: restart it after applying an update.
