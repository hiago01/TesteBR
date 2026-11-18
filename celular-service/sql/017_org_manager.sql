-- Maximo RP - Gerenciador de Grupo (migracao 017): painel do cliente (Inicio / Membros / Farm / Cofre) sobre o esquema org. Idempotente.
-- Regra de ouro (igual as outras migracoes): o servico so chama consultas/funcoes com parametros. Quem pede (conta) vem do TOKEN assinado
-- emitido pelo gamemode; as regras (permissao, saldo, hierarquia) sao validadas AQUI, no PostgreSQL.

-- ---- dados do grupo ----
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS discord      text    NOT NULL DEFAULT ''     CHECK (char_length(discord) <= 120);
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS radio        text    NOT NULL DEFAULT ''     CHECK (char_length(radio) <= 24);
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS notice       text    NOT NULL DEFAULT ''     CHECK (char_length(notice) <= 400);   -- mural de avisos
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS gps_x        real;                                                                 -- "Localizacao > Marcar no GPS" (NULL = nao cadastrada)
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS gps_y        real;
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS gps_z        real;
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS max_members  integer NOT NULL DEFAULT 60     CHECK (max_members BETWEEN 1 AND 500);
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS bank         bigint  NOT NULL DEFAULT 0      CHECK (bank >= 0);                    -- cofre (R$ inteiros)
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS farm_item    integer NOT NULL DEFAULT 0      CHECK (farm_item >= 0);               -- id do item do INVENTARIO que e o "farm" (0 = nao configurado)
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS farm_name    text    NOT NULL DEFAULT 'Coleta' CHECK (char_length(farm_name) BETWEEN 1 AND 24);
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS farm_price   integer NOT NULL DEFAULT 1      CHECK (farm_price BETWEEN 0 AND 100000);   -- R$ por unidade entregue (pago ao jogador)
ALTER TABLE org.defs ADD COLUMN IF NOT EXISTS farm_goal    integer NOT NULL DEFAULT 300    CHECK (farm_goal BETWEEN 1 AND 100000);    -- meta diaria por membro

-- ---- membros: presenca e tempo jogado NA organizacao ----
ALTER TABLE org.members ADD COLUMN IF NOT EXISTS last_login timestamptz;
ALTER TABLE org.members ADD COLUMN IF NOT EXISTS play_secs  bigint  NOT NULL DEFAULT 0;
ALTER TABLE org.members ADD COLUMN IF NOT EXISTS online     boolean NOT NULL DEFAULT false;

-- ---- permissoes por cargo (bits): 1 recrutar | 2 expulsar | 4 promover/rebaixar | 8 gerenciar cargos | 16 sacar do cofre | 32 editar mural | 64 reservado. Cargo 1 (Lider) tem tudo sempre.
ALTER TABLE org.ranks ADD COLUMN IF NOT EXISTS perms integer NOT NULL DEFAULT 0 CHECK (perms BETWEEN 0 AND 127);
UPDATE org.ranks SET perms = CASE rank WHEN 1 THEN 127 WHEN 2 THEN 127 WHEN 3 THEN 35 ELSE 0 END WHERE perms = 0;
CREATE OR REPLACE FUNCTION org.seed_ranks(p_org integer) RETURNS void LANGUAGE sql AS $$
    INSERT INTO org.ranks(org_id, rank, name, perms)
    SELECT p_org, r.n, r.nm, r.pm FROM (VALUES (1,'Líder',127),(2,'Subcomandante',127),(3,'Capitão',35),(4,'Tenente',0),(5,'Sargento',0),(6,'Soldado',0),(7,'Recruta',0)) AS r(n, nm, pm)
    ON CONFLICT DO NOTHING;
$$;

-- ---- historico do cofre e das entregas de farm ----
CREATE TABLE IF NOT EXISTS org.bank_log (
    id         bigserial PRIMARY KEY,
    org_id     integer NOT NULL REFERENCES org.defs(id) ON DELETE CASCADE,
    account_id integer NOT NULL,
    name       text    NOT NULL DEFAULT '' CHECK (char_length(name) <= 24),
    action     text    NOT NULL CHECK (action IN ('ENTRADA', 'SAIDA')),
    kind       text    NOT NULL CHECK (kind IN ('DEPOSITO', 'SAQUE')),
    amount     bigint  NOT NULL CHECK (amount > 0),
    at         timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS org_bank_log_idx ON org.bank_log(org_id, id DESC);

CREATE TABLE IF NOT EXISTS org.farm_log (
    id         bigserial PRIMARY KEY,
    org_id     integer NOT NULL REFERENCES org.defs(id) ON DELETE CASCADE,
    account_id integer NOT NULL,
    name       text    NOT NULL DEFAULT '' CHECK (char_length(name) <= 24),
    qty        integer NOT NULL CHECK (qty > 0),
    paid       bigint  NOT NULL DEFAULT 0 CHECK (paid >= 0),
    at         timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS org_farm_log_org_idx ON org.farm_log(org_id, at DESC);
CREATE INDEX IF NOT EXISTS org_farm_log_acct_idx ON org.farm_log(account_id, at DESC);

-- ---- datas no fuso de Brasilia (dia/semana do farm) ----
CREATE OR REPLACE FUNCTION org.today() RETURNS date LANGUAGE sql STABLE AS $$ SELECT (now() AT TIME ZONE 'America/Sao_Paulo')::date $$;
CREATE OR REPLACE FUNCTION org.week_start() RETURNS date LANGUAGE sql STABLE AS $$ SELECT date_trunc('week', now() AT TIME ZONE 'America/Sao_Paulo')::date $$;   -- segunda-feira
CREATE OR REPLACE FUNCTION org.local_day(p_at timestamptz) RETURNS date LANGUAGE sql IMMUTABLE AS $$ SELECT (p_at AT TIME ZONE 'America/Sao_Paulo')::date $$;

-- ---- permissao: o membro tem o bit? (Lider sempre) ----
CREATE OR REPLACE FUNCTION org.perm_ok(p_acct integer, p_perm integer) RETURNS boolean LANGUAGE sql STABLE AS $$
    SELECT coalesce((SELECT m.rank = 1 OR (coalesce(r.perms, 0) & p_perm) <> 0
                       FROM org.members m LEFT JOIN org.ranks r ON r.org_id = m.org_id AND r.rank = m.rank
                      WHERE m.account_id = p_acct), false)
$$;

-- ---- promover / rebaixar / expulsar (substitui a versao 013, que era so do Lider) ----
-- Regras: precisa do bit certo (expulsar = 2, promover/rebaixar = 4); so age sobre cargo MAIS BAIXO que o proprio; promover nao chega ao proprio cargo.
-- Resposta: ok|<novo cargo (0 = expulso)>|<org> | noperm | self | notmember | isleader | higher | limit | args
CREATE OR REPLACE FUNCTION org.rank_act(p_leader integer, p_target integer, p_act text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; t org.members%ROWTYPE; nr integer;
BEGIN
    IF p_act NOT IN ('promote', 'demote', 'kick') THEN RETURN 'args'; END IF;
    SELECT * INTO l FROM org.members WHERE account_id = p_leader;
    IF NOT FOUND THEN RETURN 'noperm'; END IF;
    IF NOT org.perm_ok(p_leader, CASE WHEN p_act = 'kick' THEN 2 ELSE 4 END) THEN RETURN 'noperm'; END IF;
    IF p_leader = p_target THEN RETURN 'self'; END IF;
    SELECT * INTO t FROM org.members WHERE account_id = p_target FOR UPDATE;
    IF NOT FOUND OR t.org_id <> l.org_id THEN RETURN 'notmember'; END IF;
    IF t.rank = 1 THEN RETURN 'isleader'; END IF;
    IF t.rank <= l.rank THEN RETURN 'higher'; END IF;
    IF p_act = 'kick' THEN
        DELETE FROM org.members WHERE account_id = p_target;
        RETURN 'ok|0|' || l.org_id;
    ELSIF p_act = 'promote' THEN
        nr := t.rank - 1;
        IF nr <= l.rank THEN RETURN 'limit'; END IF;
    ELSE
        IF t.rank >= 7 THEN RETURN 'limit'; END IF;
        nr := t.rank + 1;
    END IF;
    UPDATE org.members SET rank = nr WHERE account_id = p_target;
    RETURN 'ok|' || nr || '|' || l.org_id;
END $$;

-- ---- convite: precisa do bit 1; o alvo nao pode ter organizacao; grupo lotado = full. Resposta: ok|<org> | noperm | has | full
CREATE OR REPLACE FUNCTION org.invite_check(p_leader integer, p_target integer) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; mx integer; n integer;
BEGIN
    SELECT * INTO l FROM org.members WHERE account_id = p_leader;
    IF NOT FOUND OR NOT org.perm_ok(p_leader, 1) THEN RETURN 'noperm'; END IF;
    IF EXISTS (SELECT 1 FROM org.members WHERE account_id = p_target) THEN RETURN 'has'; END IF;
    SELECT max_members INTO mx FROM org.defs WHERE id = l.org_id;
    SELECT count(*) INTO n FROM org.members WHERE org_id = l.org_id;
    IF n >= mx THEN RETURN 'full'; END IF;
    RETURN 'ok|' || l.org_id;
END $$;

-- ---- cargos: nome (bit 8). So mexe em cargo mais baixo que o proprio (Lider: todos, inclusive o proprio nome) ----
CREATE OR REPLACE FUNCTION org.mgr_rankname(p_actor integer, p_rank integer, p_name text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; nm text := btrim(regexp_replace(coalesce(p_name, ''), '[[:cntrl:]|]+', ' ', 'g'));
BEGIN
    SELECT * INTO l FROM org.members WHERE account_id = p_actor;
    IF NOT FOUND OR NOT org.perm_ok(p_actor, 8) THEN RETURN 'noperm'; END IF;
    IF p_rank < 1 OR p_rank > 7 OR char_length(nm) NOT BETWEEN 1 AND 24 THEN RETURN 'args'; END IF;
    IF l.rank <> 1 AND p_rank <= l.rank THEN RETURN 'higher'; END IF;
    INSERT INTO org.ranks(org_id, rank, name) VALUES (l.org_id, p_rank, nm) ON CONFLICT (org_id, rank) DO UPDATE SET name = EXCLUDED.name;
    PERFORM org.bump();
    RETURN 'ok';
END $$;

-- permissoes de um cargo (bit 8). Lider (cargo 1) e fixo. So se pode dar o que se tem, e so a cargo mais baixo que o proprio.
CREATE OR REPLACE FUNCTION org.mgr_rankperm(p_actor integer, p_rank integer, p_perms integer) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; mine integer;
BEGIN
    SELECT * INTO l FROM org.members WHERE account_id = p_actor;
    IF NOT FOUND OR NOT org.perm_ok(p_actor, 8) THEN RETURN 'noperm'; END IF;
    IF p_rank < 2 OR p_rank > 7 OR p_perms < 0 OR p_perms > 127 THEN RETURN 'args'; END IF;
    IF l.rank <> 1 AND p_rank <= l.rank THEN RETURN 'higher'; END IF;
    SELECT CASE WHEN l.rank = 1 THEN 127 ELSE coalesce((SELECT perms FROM org.ranks WHERE org_id = l.org_id AND rank = l.rank), 0) END INTO mine;
    IF (p_perms & ~mine) <> 0 THEN RETURN 'noperm'; END IF;
    UPDATE org.ranks SET perms = p_perms WHERE org_id = l.org_id AND rank = p_rank;
    IF NOT FOUND THEN RETURN 'args'; END IF;
    RETURN 'ok';
END $$;

-- ---- mural de avisos (bit 32) ----
CREATE OR REPLACE FUNCTION org.mgr_notice(p_actor integer, p_text text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; t text := btrim(regexp_replace(coalesce(p_text, ''), '[\x01-\x08\x0b\x0c\x0e-\x1f]+', '', 'g'));
BEGIN
    SELECT * INTO l FROM org.members WHERE account_id = p_actor;
    IF NOT FOUND OR NOT org.perm_ok(p_actor, 32) THEN RETURN 'noperm'; END IF;
    IF char_length(t) > 400 THEN RETURN 'args'; END IF;
    UPDATE org.defs SET notice = t, updated_at = now() WHERE id = l.org_id;
    RETURN 'ok';
END $$;

-- ---- cofre: deposito (qualquer membro) e saque (bit 16). Atomico; o dinheiro do JOGADOR e movido pelo gamemode (Pawn). Resposta: ok|<saldo> | nomember | noperm | nofunds | args
CREATE OR REPLACE FUNCTION org.mgr_bank(p_actor integer, p_kind text, p_amount bigint, p_name text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; nb bigint;
BEGIN
    IF p_kind NOT IN ('deposit', 'withdraw') OR p_amount IS NULL OR p_amount < 1 OR p_amount > 1000000000 THEN RETURN 'args'; END IF;
    SELECT * INTO l FROM org.members WHERE account_id = p_actor;
    IF NOT FOUND THEN RETURN 'nomember'; END IF;
    IF p_kind = 'withdraw' THEN
        IF NOT org.perm_ok(p_actor, 16) THEN RETURN 'noperm'; END IF;
        UPDATE org.defs SET bank = bank - p_amount WHERE id = l.org_id AND bank >= p_amount RETURNING bank INTO nb;
        IF NOT FOUND THEN RETURN 'nofunds'; END IF;
        INSERT INTO org.bank_log(org_id, account_id, name, action, kind, amount) VALUES (l.org_id, p_actor, left(coalesce(p_name, l.name), 24), 'SAIDA', 'SAQUE', p_amount);
    ELSE
        UPDATE org.defs SET bank = bank + p_amount WHERE id = l.org_id RETURNING bank INTO nb;
        INSERT INTO org.bank_log(org_id, account_id, name, action, kind, amount) VALUES (l.org_id, p_actor, left(coalesce(p_name, l.name), 24), 'ENTRADA', 'DEPOSITO', p_amount);
    END IF;
    RETURN 'ok|' || nb;
END $$;

-- ---- entrega de farm: registra e devolve quanto o jogador recebe. Resposta: ok|<id do registro>|<pago>|<qtd de hoje>|<meta> | nomember | nofarm | args
CREATE OR REPLACE FUNCTION org.mgr_farm(p_actor integer, p_qty integer, p_name text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE l org.members%ROWTYPE; d org.defs%ROWTYPE; pay bigint; lid bigint; today integer;
BEGIN
    IF p_qty IS NULL OR p_qty < 1 OR p_qty > 100000 THEN RETURN 'args'; END IF;
    SELECT * INTO l FROM org.members WHERE account_id = p_actor;
    IF NOT FOUND THEN RETURN 'nomember'; END IF;
    SELECT * INTO d FROM org.defs WHERE id = l.org_id;
    IF d.farm_item <= 0 THEN RETURN 'nofarm'; END IF;
    pay := p_qty::bigint * d.farm_price;
    INSERT INTO org.farm_log(org_id, account_id, name, qty, paid) VALUES (l.org_id, p_actor, left(coalesce(p_name, l.name), 24), p_qty, pay) RETURNING id INTO lid;
    SELECT coalesce(sum(qty), 0) INTO today FROM org.farm_log WHERE account_id = p_actor AND org.local_day(at) = org.today();
    RETURN 'ok|' || lid || '|' || pay || '|' || today || '|' || d.farm_goal;
END $$;
-- desfaz uma entrega (o gamemode nao conseguiu tirar o item do inventario)
CREATE OR REPLACE FUNCTION org.mgr_farm_undo(p_actor integer, p_id bigint) RETURNS integer LANGUAGE sql AS $$
    WITH d AS (DELETE FROM org.farm_log WHERE id = p_id AND account_id = p_actor RETURNING 1) SELECT count(*)::integer FROM d
$$;

-- ---- presenca: 1 entrou | 0 saiu (soma o resto do tempo) | 2 tique (soma p_secs de tempo jogado). So altera quem e membro. ----
CREATE OR REPLACE FUNCTION org.mgr_presence(p_acct integer, p_state integer, p_secs integer) RETURNS integer LANGUAGE plpgsql AS $$
DECLARE n integer;
BEGIN
    IF p_state = 1 THEN UPDATE org.members SET online = true, last_login = now() WHERE account_id = p_acct;
    ELSIF p_state = 0 THEN UPDATE org.members SET online = false, play_secs = play_secs + greatest(0, least(coalesce(p_secs, 0), 3600)) WHERE account_id = p_acct;
    ELSIF p_state = 2 THEN UPDATE org.members SET online = true, play_secs = play_secs + greatest(0, least(coalesce(p_secs, 0), 3600)) WHERE account_id = p_acct;
    ELSE RETURN 0; END IF;
    GET DIAGNOSTICS n = ROW_COUNT;
    RETURN n;
END $$;
-- o servidor reiniciou: ninguem esta online ate o jogador entrar de novo
CREATE OR REPLACE FUNCTION org.mgr_presence_reset() RETURNS integer LANGUAGE sql AS $$
    WITH u AS (UPDATE org.members SET online = false WHERE online RETURNING 1) SELECT count(*)::integer FROM u
$$;

INSERT INTO phone.schema_version(v) VALUES (17) ON CONFLICT DO NOTHING;
