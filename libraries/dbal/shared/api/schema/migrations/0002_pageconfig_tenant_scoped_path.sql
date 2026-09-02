-- 0002_pageconfig_tenant_scoped_path
--
-- PageConfig.path had a *global* unique index -- across every tenant, not
-- per tenant -- so no two tenants could ever both have a page at the same
-- path. Every tenant naturally wants a homepage at "/", so this made
-- multi-tenant page building fail outright the moment a second tenant tried
-- to create one: "ERROR: duplicate key value violates unique constraint
-- idx_pageconfig_path". Found by actually using the God Panel's page
-- builder as a second tenant, not by inspection.
--
-- Idempotent throughout, and safe to re-run.

DROP INDEX IF EXISTS "idx_pageconfig_path";
CREATE UNIQUE INDEX IF NOT EXISTS "idx_pageconfig_tenantid_path"
  ON "PageConfig" ("tenantId", "path");
