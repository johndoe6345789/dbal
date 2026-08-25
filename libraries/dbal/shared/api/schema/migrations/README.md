# Migrations

What the entity schemas cannot say.

DBAL builds every table from its entity definition, but the create-table
template (`templates/sql/*_create_table.sql.j2`) emits columns and nothing
else. That leaves three gaps, and they all land here:

- **Foreign keys.** An entity's `foreign_key` and `relations` describe intent;
  nothing creates a constraint, so nothing enforces it.
- **Index changes.** Indexes are created but never dropped, so a uniqueness
  rule outlives the schema that declared it.
- **Removals.** A column stays after the entity stops declaring it. Left
  alone, a document column reappears on the next restart.

## Rules

- One file per change, named `NNNN_short_description.sql`, applied in
  filename order.
- Write every statement so re-running is harmless: `IF EXISTS`,
  `IF NOT EXISTS`, `DROP CONSTRAINT` before `ADD CONSTRAINT`.
- Never edit a file that has been applied. The runner records a checksum and
  refuses a file that changed, because at that point the database and the
  repository disagree and only a human knows which is right. Add a new file.

## Running them

The deploy applies them once the stack is healthy. By hand:

    cd /srv/repos/deployment
    python3 deployment.py migrate --dry-run   # what is pending
    python3 deployment.py migrate             # apply

State lives in the `schema_migrations` table: file name, checksum, and when
it ran.
