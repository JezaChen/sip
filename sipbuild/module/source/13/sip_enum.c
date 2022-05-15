/*
 * This file implements the enum support.
 *
 * Copyright (c) 2022 Riverbank Computing Limited <info@riverbankcomputing.com>
 *
 * This file is part of SIP.
 *
 * This copy of SIP is licensed for use under the terms of the SIP License
 * Agreement.  See the file LICENSE for more details.
 *
 * This copy of SIP may also used under the terms of the GNU General Public
 * License v2 or v3 as published by the Free Software Foundation which can be
 * found in the files LICENSE-GPL2 and LICENSE-GPL3 included in this package.
 *
 * SIP is supplied WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */


#include <Python.h>

#include <assert.h>

#include "sipint.h"

#include "sip_enum.h"


#define IS_UNSIGNED_ENUM(etd)   ((etd)->etd_base_type == SIP_ENUM_UINT_ENUM || (etd)->etd_base_type == SIP_ENUM_INT_FLAG || (etd)->etd_base_type == SIP_ENUM_FLAG)


static PyObject *enum_type = NULL;      /* The enum.Enum type. */
static PyObject *int_enum_type = NULL;  /* The enum.IntEnum type. */
static PyObject *flag_type = NULL;      /* The enum.Flag type. */
static PyObject *int_flag_type = NULL;  /* The enum.IntFlag type. */

static PyObject *str_dunder_sip = NULL; /* '__sip__' */
static PyObject *str_module = NULL;     /* 'module' */
static PyObject *str_qualname = NULL;   /* 'qualname' */
static PyObject *str_value = NULL;      /* 'value' */


/* Forward references. */
static PyObject *create_enum_object(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, sipIntInstanceDef **next_int_p, PyObject *name);
static void enum_expected(PyObject *obj, const sipTypeDef *td);
static PyObject *get_enum_type(const sipTypeDef *td);


/*
 * Create a Python object for a member of a named enum.
 */
PyObject *sip_api_convert_from_enum(int member, const sipTypeDef *td)
{
    PyObject *et;

    assert(sipTypeIsEnum(td));

    et = get_enum_type(td);

    return PyObject_CallFunction(et,
            IS_UNSIGNED_ENUM((sipEnumTypeDef *)td) ? "(I)" : "(i)", member);
}


/*
 * Convert a Python object implementing an enum to an integer value.  An
 * exception is raised if there was an error.
 */
int sip_api_convert_to_enum(PyObject *obj, const sipTypeDef *td)
{
    PyObject *val_obj, *type_obj;
    int val;

    assert(sipTypeIsEnum(td));

    /* Make sure the enum object has been created. */
    type_obj = get_enum_type(td);

    /* Check the type of the Python object. */
    if (PyObject_IsInstance(obj, type_obj) <= 0)
    {
        enum_expected(obj, td);
        return -1;
    }

    /* Get the value from the object. */
    if ((val_obj = PyObject_GetAttr(obj, str_value)) == NULL)
        return -1;

    /* Flags are implicitly unsigned. */
    if (IS_UNSIGNED_ENUM((sipEnumTypeDef *)td))
        val = (int)sip_api_long_as_unsigned_int(val_obj);
    else
        val = sip_api_long_as_int(val_obj);

    Py_DECREF(val_obj);

    return val;
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Flag.
 */
int sip_api_is_enum_flag(PyObject *obj)
{
    return (PyObject_IsSubclass(obj, flag_type) == 1);
}


/*
 * Create an enum object and add it to a dictionary.  A negative value is
 * returned (and an exception set) if there was an error.
 */
int sip_enum_create(sipExportedModuleDef *client, sipEnumTypeDef *etd,
        sipIntInstanceDef **next_int_p, PyObject *dict)
{
    int rc;
    PyObject *name, *enum_obj;

    /* Create an object corresponding to the type name. */
    if ((name = PyUnicode_FromString(sipPyNameOfEnum(etd))) == NULL)
        return -1;

    /* Create the enum object. */
    if ((enum_obj = create_enum_object(client, etd, next_int_p, name)) == NULL)
    {
        Py_DECREF(name);
        return -1;
    }

    /* Add the enum to the "parent" dictionary. */
    rc = PyDict_SetItem(dict, name, enum_obj);

    /* We can now release our remaining references. */
    Py_DECREF(name);
    Py_DECREF(enum_obj);

    return rc;
}


/*
 * Return the generated type structure for a Python enum object that wraps a
 * C/C++ enum or NULL (and no exception set) if the object is something else.
 */
const sipTypeDef *sip_enum_get_generated_type(PyObject *obj)
{
    if (sip_enum_is_enum(obj))
    {
        PyObject *etd_cap;

        if ((etd_cap = PyObject_GetAttr(obj, str_dunder_sip)) != NULL)
        {
            sipTypeDef *td = (sipTypeDef *)PyCapsule_GetPointer(etd_cap, NULL);

            Py_DECREF(etd_cap);

            return td;
        }

        PyErr_Clear();
    }

    return NULL;
}


/*
 * Initialise the enum support.  A negative value is returned (and an exception
 * set) if there was an error.
 */
int sip_enum_init(void)
{
    PyObject *obj;

    /* Get the enum types. */
    if ((obj = PyImport_ImportModule("enum")) == NULL)
        return -1;

    enum_type = PyObject_GetAttrString(obj, "Enum");
    int_enum_type = PyObject_GetAttrString(obj, "IntEnum");
    flag_type = PyObject_GetAttrString(obj, "Flag");
    int_flag_type = PyObject_GetAttrString(obj, "IntFlag");

    Py_DECREF(obj);

    if (enum_type == NULL || int_enum_type == NULL || flag_type == NULL || int_flag_type == NULL)
    {
        Py_XDECREF(enum_type);
        Py_XDECREF(int_enum_type);
        Py_XDECREF(flag_type);
        Py_XDECREF(int_flag_type);

        return -1;
    }

    /* Objectify the strings. */
    if (sip_objectify("__sip__", &str_dunder_sip) < 0)
        return -1;

    if (sip_objectify("module", &str_module) < 0)
        return -1;

    if (sip_objectify("qualname", &str_qualname) < 0)
        return -1;

    if (sip_objectify("value", &str_value) < 0)
        return -1;

    return 0;
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Enum.
 */
int sip_enum_is_enum(PyObject *obj)
{
    return (PyObject_IsSubclass(obj, enum_type) == 1);
}



/*
 * Create an enum object.
 */
static PyObject *create_enum_object(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, sipIntInstanceDef **next_int_p, PyObject *name)
{
    int i;
    PyObject *members, *enum_factory, *enum_obj, *args, *kw_args, *etd_cap;
    sipIntInstanceDef *next_int;

    /* Create a dict of the members. */
    if ((members = PyDict_New()) == NULL)
        goto ret_err;

    next_int = *next_int_p;
    assert(next_int != NULL);

    for (i = 0; i < etd->etd_nr_members; ++i)
    {
        PyObject *value_obj;

        assert(next_int->ii_name != NULL);

        /* Flags are implicitly unsigned. */
        if (IS_UNSIGNED_ENUM(etd))
            value_obj = PyLong_FromUnsignedLong((unsigned)next_int->ii_val);
        else
            value_obj = PyLong_FromLong(next_int->ii_val);

        if (sip_dict_set_and_discard(members, next_int->ii_name, value_obj) < 0)
            goto rel_members;

        ++next_int;
    }

    *next_int_p = next_int;

    if ((args = PyTuple_Pack(2, name, members)) == NULL)
        goto rel_members;

    if ((kw_args = PyDict_New()) == NULL)
        goto rel_args;

    if (PyDict_SetItem(kw_args, str_module, client->em_nameobj) < 0)
        goto rel_kw_args;

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     if (etd->etd_scope >= 0)
     {
        int rc;
        PyObject *qualname;

        if ((qualname = sip_get_qualname(client->em_types[etd->etd_scope], name)) == NULL)
            goto rel_kw_args;

        rc = PyDict_SetItem(kw_args, str_qualname, qualname);

        Py_DECREF(qualname);

        if (rc < 0)
            goto rel_kw_args;
    }

    /* Wrap the type definition in a capsule. */
    if ((etd_cap = PyCapsule_New(etd, NULL, NULL)) == NULL)
        goto rel_kw_args;

    if (etd->etd_base_type == SIP_ENUM_INT_FLAG)
        enum_factory = int_flag_type;
    else if (etd->etd_base_type == SIP_ENUM_FLAG)
        enum_factory = flag_type;
    else if (etd->etd_base_type == SIP_ENUM_INT_ENUM || etd->etd_base_type == SIP_ENUM_UINT_ENUM)
        enum_factory = int_enum_type;
    else
        enum_factory = enum_type;

    if ((enum_obj = PyObject_Call(enum_factory, args, kw_args)) == NULL)
        goto rel_kw_args;

    Py_DECREF(kw_args);
    Py_DECREF(args);
    Py_DECREF(members);

    /* Note that it isn't actually a PyTypeObject. */
    etd->etd_base.td_py_type = (PyTypeObject *)enum_obj;

    if (PyObject_SetAttr(enum_obj, str_dunder_sip, etd_cap) < 0)
    {
        Py_DECREF(etd_cap);
        Py_DECREF(enum_obj);
        return NULL;
    }

    Py_DECREF(etd_cap);

    if (etd->etd_pyslots != NULL)
        sip_add_type_slots((PyHeapTypeObject *)enum_obj, etd->etd_pyslots);

    return enum_obj;

    /* Unwind on errors. */

rel_kw_args:
    Py_DECREF(kw_args);

rel_args:
    Py_DECREF(args);

rel_members:
    Py_DECREF(members);

ret_err:
    return NULL;
}


/*
 * Raise an exception when failing to convert an enum because of its type.
 */
static void enum_expected(PyObject *obj, const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "a member of enum '%s' is expected not '%s'",
            sipPyNameOfEnum((sipEnumTypeDef *)td), Py_TYPE(obj)->tp_name);
}


/*
 * Get the Python object for an enum type.
 */
static PyObject *get_enum_type(const sipTypeDef *td)
{
    PyObject *type_obj;

    /* Make sure the enum object has been created. */
    type_obj = (PyObject *)sipTypeAsPyTypeObject(td);

    if (type_obj == NULL)
    {
        if (sip_add_all_lazy_attrs(sip_api_type_scope(td)) < 0)
            return NULL;

        type_obj = (PyObject *)sipTypeAsPyTypeObject(td);
    }

    return type_obj;
}
