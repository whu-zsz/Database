create table student (id int, name char(8), score float);
insert into student values (1, 'xiaohong', 90.0);
begin;
insert into student values (2, 'xiaoming', 99.0);
update student set score = 100.0 where id = 1;
commit;
select * from student;
