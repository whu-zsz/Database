create table t(id int, time datetime);
insert into t values(1, '2023-05-18 09:12:19');
insert into t values(2, '2023-05-30 12:34:32');
select * from t;
delete from t where time = '2023-05-30 12:34:32';
update t set id = 2023 where time = '2023-05-18 09:12:19';
select * from t;
