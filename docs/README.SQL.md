Introduction
============

ToastStunt comes with the ability to support multiple SQL server types. 
Each client requires the appropriate library to be installed when cmake 
is executed.

As of this version of the SQL implementation, the following types are
supported:

* MySQL/MariaDB
* PostgreSQL
* MSSQL
* SQLiteV3

This document explains how to build and deploy with SQL.

How SQL can be used
===================

No part of ToastStunt natively uses SQL for storage or propagation.
Installing or enabling SQL will not move away from the flatfile database
format, and no connection strings are hardcoded into ToastStunt.

Instead, SQL can be used to augment data storage and indexing
outside of the MOO. Doing so can allow that data to be accessed by
external applications (websites, other services), and because of the
non-blocking nature of SQL support, can be used to offload that data
out of memory.

For example, a ticket system could be used that has an in game interface, 
or a website interface.

A pose system that records poses and has a website interface for
viewing them is another example, where storing that data in MOO
may cause performance complications.

Compiling with SQL
==================

To enable SQL support with ToastStunt, it is first necessary to
install the related client libraries. These libraries are listed below in
the relevant subsections depending on which server types you wish to 
support.

As many or as few libraries may be installed as desired. It possible
to enable all server types, or disable all of them.

After the client libraries are installed, it's only necessary to
run cmake as you would normally on ToastStunt and compile!

MySQL/MariaDB
-------------

### **Debian/Ubuntu/WSL**
```bash
apt-get install libmysqlcppconn-dev
```

PostgreSQL
----------

MSSQL
-----

SQLiteV3
--------
