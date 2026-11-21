-- 0025 AutoPDMS 权益独立
--
-- 背景
--   2026-11-21 起 UEAICAD 订阅不再隐含 AutoPDMS；
--   存量 UEAICAD 正式订阅与试用均为打包购买，需要补一条同起止日的 AutoPDMS 记录，
--   否则上线后存量客户会立刻丢失建模/出图能力。
--
-- 规则
--   每团队取最新一条 status=1 的 ueaicad 记录，克隆为 product='pdms'、plan='network'；
--   已有 status=1 的 pdms 记录（含 PDMS-only）不重复补；
--   is_trial 原样保留，保证试用与付费到期口径一致。

insert into team_subscription
    (team_id, product, plan, start_date, end_date, status, is_trial)
select s.team_id, 'pdms', 'network', s.start_date, s.end_date, 1, s.is_trial
from (
    select distinct on (team_id)
        team_id, start_date, end_date, coalesce(is_trial, false) as is_trial
    from team_subscription
    where product = 'ueaicad' and status = 1
    order by team_id, end_date desc, id desc
) s
where not exists (
    select 1 from team_subscription p
    where p.team_id = s.team_id and p.product = 'pdms' and p.status = 1
);
