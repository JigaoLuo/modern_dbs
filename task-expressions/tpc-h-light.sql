CREATE TABLE customer
(
    c_custkey    INTEGER PRIMARY KEY,
    c_name       CHAR(25),
    c_address    CHAR(40),
    c_nationkey  INTEGER,
    c_phone      CHAR(15),
    c_acctbal    INTEGER,
    c_mktsegment CHAR(10),
    c_comment    CHAR(117)
);
CREATE TABLE nation
(
    n_nationkey INTEGER PRIMARY KEY,
    n_name      CHAR(25),
    n_regionkey INTEGER,
    n_comment   CHAR(152)
);
CREATE TABLE region
(
    r_regionkey INTEGER PRIMARY KEY,
    r_name      CHAR(25),
    r_comment   CHAR(152)
);
