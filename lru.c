#include <Python.h>

/*
 * This is a simple implementation of LRU Dict that uses a Python dict and an associated doubly linked
 * list to keep track of recently inserted/accessed items.
 *
 * Dict will store: key -> Node mapping, where Node is a linked list node.
 * The Node itself will contain the value as well as the key.
 *
 * For eg:
 *
 * >>> l = LRU(2)
 * >>> l[0] = 'foo'
 * >>> l[1] = 'bar'
 *
 * can be visualised as:
 *
 *             ---+--(hash(0)--+--hash(1)--+
 * self->dict  ...|            |           |
 *             ---+-----|------+---------|-+
 *                      |                |
 *                +-----v------+   +-----v------+
 * self->first--->|<'foo'>, <0>|-->|<'bar'>, <1>|<---self->last
 *             +--|            |<--|            |--+
 *             |  +------------+   +------------+  |
 *             v                                   v
 *           NULL                                 NULL
 *
 *  The invariant is to maintain the list to reflect the LRU order of items in the dict.
 *  self->first will point to the MRU item and self-last to LRU item. Size of list will not
 *  grow beyond size of LRU dict.
 *
 *  NOTE: No matter whether threading/locking is used internally in the
 *  implementation of LRU, the concept of critical section is relevant, in
 *  particular in the presence of the callback. The callback should not be
 *  allowed to execute inside the critical section where the internal data is
 *  being modified. Instead, the evicted objects are temporarily stored in a
 *  staging list, which is purged when necessary, during which the callback is
 *  called.
 *
 *  Here, the functions beginning with capital-letter "LRU_" are meant to be
 *  exposed to Python as methods. The small-letter "lru_" functions are
 *  implementation details and they will operate within the critical section.
 *  Ideally, each method function should leave the critical section, and only
 *  after that shall it trigger purging.
 *
 */

#ifndef Py_TYPE
 #define Py_TYPE(ob) (((PyObject*)(ob))->ob_type)
#endif

#define GET_NODE(d, key) (Node *) Py_TYPE(d)->tp_as_mapping->mp_subscript((d), (key))
#define PUT_NODE(d, key, node) Py_TYPE(d)->tp_as_mapping->mp_ass_subscript((d), (key), ((PyObject *)node))

/* If someone figures out how to enable debug builds with setuptools, you can delete this */
#if 0
#undef assert
#define str(s) #s
#define assert(v) \
  do {                                                                                \
    if (!(v)) {                                                                       \
      fprintf(stderr, "Assertion failed: %s on %s:%d\n",                              \
              str(v), __FILE__, __LINE__);                                            \
      fflush(stderr);                                                                 \
      abort();                                                                        \
    }                                                                                 \
  } while(0)
#endif

typedef struct _Node {
    PyObject_HEAD
    PyObject * value;
    PyObject * key;
    struct _Node * prev;
    struct _Node * next;
} Node;

static void
node_dealloc(Node* self)
{
    Py_DECREF(self->key);
    Py_DECREF(self->value);
    assert(self->prev == NULL);
    assert(self->next == NULL);
    PyObject_Del((PyObject*)self);
}

static PyObject*
node_repr(Node* self)
{
    return PyObject_Repr(self->value);
}

static PyTypeObject NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "lru.Node",              /* tp_name */
    sizeof(Node),            /* tp_basicsize */
    0,                       /* tp_itemsize */
    (destructor)node_dealloc,/* tp_dealloc */
    0,                       /* tp_print */
    0,                       /* tp_getattr */
    0,                       /* tp_setattr */
    0,                       /* tp_compare */
    (reprfunc)node_repr,     /* tp_repr */
    0,                       /* tp_as_number */
    0,                       /* tp_as_sequence */
    0,                       /* tp_as_mapping */
    0,                       /* tp_hash */
    0,                       /* tp_call */
    0,                       /* tp_str */
    0,                       /* tp_getattro */
    0,                       /* tp_setattro */
    0,                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,      /* tp_flags */
    "Linked List Node",      /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    0,                       /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    0,                       /* tp_init */
    0,                       /* tp_alloc */
    0,                       /* tp_new */
};

typedef struct {
    PyObject_HEAD
    PyObject * dict;
    Node * first;
    Node * last;
    Py_ssize_t size;
    Py_ssize_t hits;
    Py_ssize_t misses;
    PyObject *callback;
    PyObject *staging_list;
    char purge_flag;
} LRU;

/* The pair of macros currently do nothing. They simply add annotation in the
 * source of the LRU_* method functions. */
#define LRU_ENTER_CRITICAL
#define LRU_LEAVE_CRITICAL

static PyObject *
set_callback(LRU *self, PyObject *args)
{
    PyObject *result = NULL;
    PyObject *temp;

    if (PyArg_ParseTuple(args, "O:set_callback", &temp)) {
        if (temp == Py_None) {
            Py_XDECREF(self->callback);
            self->callback = NULL;
        } else if (!PyCallable_Check(temp)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return NULL;
        } else {
            Py_XINCREF(temp);         /* Add a reference to new callback */
            Py_XDECREF(self->callback);  /* Dispose of previous callback */
            self->callback = temp;       /* Remember new callback */
        }
        Py_RETURN_NONE;
    }
    return result;
}

static void
lru_remove_node(LRU *self, Node* node)
{
    if (self->first == node) {
        self->first = node->next;
    }
    if (self->last == node) {
        self->last = node->prev;
    }
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    node->next = node->prev = NULL;
}

static void
lru_add_node_at_head(LRU *self, Node* node)
{
    node->prev = NULL;
    if (!self->first) {
        self->first = self->last = node;
        node->next = NULL;
    } else {
        node->next = self->first;
        if (node->next) {
            node->next->prev = node;
        }
        self->first = node;
    }
}

static void
lru_delete_last(LRU *self)
{
    Node* n = self->last;
    if (n == NULL)
        return;

    /* Transfer the node to staging. */
    if (self->callback && self->staging_list) {
	/* The list will increase the refcount to the node if successful */
	if (PyList_Append(self->staging_list, (PyObject *)n) != -1)
	    self->purge_flag |= 1;	/* set "to-purge" flag */
    }

    lru_remove_node(self, n);
    PUT_NODE(self->dict, n->key, NULL);
}


static void
lru_purge_staging(LRU *self)
{
    /* Purging mechanism.
     *
     * NOTE: This is _the_ place where callback gets called. The function is
     * intended to be always called _outside_ the critical section. The reason
     * is that we cannot guarantee the callback function, which can do
     * anything, will play nice with the LRU object itself. If the callback
     * were executed within the critical section where/while the LRU internal
     * data is not consistent, the inconsistency can be exposed.
     *
     * The staging area is just a Python list, and if a callback is set, it is
     * called along the list in ascending index order. Then the list elements
     * are wiped.
     */
    if (!self->purge_flag)
	return;
    self->purge_flag = 0;
    if (self->staging_list) {
	Py_ssize_t len = PyList_Size(self->staging_list);

	if (self->callback != NULL) {
	    Py_ssize_t i;
	    for (i = 0; i < len; i++) {
		Node *args;

		/* new reference */
		args = (Node *)PySequence_GetItem(self->staging_list, i);

		if (args) {
		    PyObject *result;
		    result = PyObject_CallFunction(self->callback,
			    "OO", args->key, args->value);
		    Py_XDECREF(result);
		} else {
		    PyErr_Clear();
		}
		/* dispose of new reference */
		Py_XDECREF(args);
	    }
	}
	if (PySequence_DelSlice(self->staging_list, 0, len) == -1)
	    PyErr_Clear();
    }
}

static Py_ssize_t
lru_length(LRU *self)
{
    return PyDict_Size(self->dict);
}

/* Currently the public method is the same as the private implementation. */
#define LRU_length	lru_length

static PyObject *
LRU_contains_key(LRU *self, PyObject *key)
{
    int flag;

    LRU_ENTER_CRITICAL;
    flag = PyDict_Contains(self->dict, key);
    LRU_LEAVE_CRITICAL;

    if (flag == 1) {
        Py_RETURN_TRUE;
    } else if (flag == 0) {
        Py_RETURN_FALSE;
    } else {
	return NULL;
    }
}


static PyObject *
LRU_contains(LRU *self, PyObject *args)
{
    PyObject *key;
    if (!PyArg_ParseTuple(args, "O", &key))
        return NULL;
    return LRU_contains_key(self, key);
}

static int
LRU_seq_contains(LRU *self, PyObject *key)
{
    int res;

    LRU_ENTER_CRITICAL;
    res = PyDict_Contains(self->dict, key);
    LRU_LEAVE_CRITICAL;

    return res;
}

static PyObject *
lru_subscript(LRU *self, register PyObject *key)
{
    Node *node = GET_NODE(self->dict, key);
    if (!node) {
        self->misses++;
        return NULL;
    }

    assert(PyObject_TypeCheck(node, &NodeType));

    /* We don't need to move the node when it's already self->first. */
    if (node != self->first) {
        lru_remove_node(self, node);
        lru_add_node_at_head(self, node);
    }

    self->hits++;
    Py_INCREF(node->value);
    Py_DECREF(node);
    return node->value;
}


static PyObject *
LRU_subscript(LRU *self, register PyObject *key)
{
    PyObject *result;

    LRU_ENTER_CRITICAL;
    result = lru_subscript(self, key);
    LRU_LEAVE_CRITICAL;

    return result;
}


static PyObject *
LRU_get(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *instead = NULL;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "O|O", &key, &instead))
        return NULL;

    LRU_ENTER_CRITICAL;

    result = lru_subscript(self, key);

    PyErr_Clear();  /* GET_NODE sets an exception on miss. Shut it up. */
    LRU_LEAVE_CRITICAL;

    if (result)
        return result;

    if (!instead) {
        Py_RETURN_NONE;
    }

    Py_INCREF(instead);
    return instead;
}

static int
lru_ass_sub(LRU *self, PyObject *key, PyObject *value)
{
    int res = 0;
    Node *node = GET_NODE(self->dict, key);
    PyErr_Clear();  /* GET_NODE sets an exception on miss. Shut it up. */

    if (value) {
        if (node) {
            Py_INCREF(value);
            Py_DECREF(node->value);
            node->value = value;

            lru_remove_node(self, node);
            lru_add_node_at_head(self, node);

            res = 0;
        } else {
            node = PyObject_NEW(Node, &NodeType);
            node->key = key;
            node->value = value;
            node->next = node->prev = NULL;

            Py_INCREF(key);
            Py_INCREF(value);

            res = PUT_NODE(self->dict, key, node);
            if (res == 0) {
                if (lru_length(self) > self->size) {
                    lru_delete_last(self);
                }

                lru_add_node_at_head(self, node);
            }
        }
    } else {
        res = PUT_NODE(self->dict, key, NULL);
        if (res == 0) {
            assert(node && PyObject_TypeCheck(node, &NodeType));
            lru_remove_node(self, node);
        }
    }

    Py_XDECREF(node);
    return res;
}


static int
LRU_ass_sub(LRU *self, PyObject *key, PyObject *value)
{
    int res;

    LRU_ENTER_CRITICAL;
    res = lru_ass_sub(self, key, value);
    LRU_LEAVE_CRITICAL;
    /* This is an assignment method and may trigger eviction */
    lru_purge_staging(self);

    return res;
}


static PyMappingMethods LRU_as_mapping = {
    (lenfunc)LRU_length,	/*mp_length*/
    (binaryfunc)LRU_subscript,	/*mp_subscript*/
    (objobjargproc)LRU_ass_sub,	/*mp_ass_subscript*/
};

static PyObject *
collect(LRU *self, PyObject * (*getterfunc)(Node *))
{
    register PyObject *v;
    Node *curr;
    int i;
    v = PyList_New(lru_length(self));
    if (v == NULL)
        return NULL;
    curr = self->first;
    i = 0;

    while (curr) {
        PyList_SET_ITEM(v, i++, getterfunc(curr));
        curr = curr->next;
    }
    assert(i == lru_length(self));
    return v;
}

static PyObject *
get_key(Node *node)
{
    Py_INCREF(node->key);
    return node->key;
}

static PyObject *
LRU_update(LRU *self, PyObject *args, PyObject *kwargs)
{
    PyObject *key, *value;
    PyObject *arg = NULL;
    Py_ssize_t pos = 0;

    LRU_ENTER_CRITICAL;

    if (PyArg_ParseTuple(args, "|O", &arg)) {
	if (arg && PyDict_Check(arg)) {
	    while (PyDict_Next(arg, &pos, &key, &value))
		lru_ass_sub(self, key, value);
	}
    }

    if (kwargs != NULL && PyDict_Check(kwargs)) {
	while (PyDict_Next(kwargs, &pos, &key, &value))
	    lru_ass_sub(self, key, value);
    }

    LRU_LEAVE_CRITICAL;
    /* This is an assignment method and may trigger eviction */
    lru_purge_staging(self);

    Py_RETURN_NONE;
}

static PyObject *
LRU_setdefault(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *default_obj = NULL;
    PyObject *result;
    int status;

    if (!PyArg_ParseTuple(args, "O|O", &key, &default_obj))
        return NULL;

    LRU_ENTER_CRITICAL;

    result = lru_subscript(self, key);

    if (result) {
	LRU_LEAVE_CRITICAL;
	return result;
    }

    PyErr_Clear();
    if (!default_obj)
	default_obj = Py_None;

    status = lru_ass_sub(self, key, default_obj);

    LRU_LEAVE_CRITICAL;
    /* This code branch may trigger eviction by adding a new key. */
    lru_purge_staging(self);

    if (status != 0)
	return NULL;

    Py_INCREF(default_obj);
    return default_obj;
}

static PyObject *
LRU_pop(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *default_obj = NULL;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "O|O", &key, &default_obj))
        return NULL;

    LRU_ENTER_CRITICAL;
    /* Trying to access the item by key. */
    result = lru_subscript(self, key);

    if (result)
        /* result != NULL, delete it from dict by key */
        lru_ass_sub(self, key, NULL);
    else if (default_obj) {
        /* result == NULL, i.e. key missing, and default_obj given */
        PyErr_Clear();
        Py_INCREF(default_obj);
        result = default_obj;
    }
    /* Otherwise (key missing, and default_obj not given [i.e. == NULL]), the
     * call to lru_subscript (at the location marked by "Trying to access the
     * item by key" in the comments) has already generated the appropriate
     * exception. */
    LRU_LEAVE_CRITICAL;
    /* the pop() method never increases the cache size, and purging is not
     * necessary even in the case of eager purging. */

    return result;
}


static PyObject *
get_item(Node *node)
{
    PyObject *tuple = PyTuple_New(2);
    Py_INCREF(node->key);
    PyTuple_SET_ITEM(tuple, 0, node->key);
    Py_INCREF(node->value);
    PyTuple_SET_ITEM(tuple, 1, node->value);
    return tuple;
}


static PyObject *
LRU_peek_first_item(LRU *self)
{
    PyObject *result;

    LRU_ENTER_CRITICAL;

    if (self->first) {
	result = get_item(self->first);	/* New reference */
    } else {
	result = Py_None;
	Py_INCREF(result);
    }

    LRU_LEAVE_CRITICAL;

    return result;
}

static PyObject *
LRU_peek_last_item(LRU *self)
{
    PyObject *result;

    LRU_ENTER_CRITICAL;

    if (self->last) {
        result = get_item(self->last);	/* New reference */
    } else {
	result = Py_None;
	Py_INCREF(result);
    }

    LRU_LEAVE_CRITICAL;

    return result;
}

static PyObject *
LRU_popitem(LRU *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"least_recent", NULL};
    int pop_least_recent = 1;
    PyObject *item_to_pop;	/* Python tuple of (key, value) */
    Node *node;

#if PY_MAJOR_VERSION >= 3
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p", kwlist, &pop_least_recent))
        return NULL;
#else
    {
        PyObject *arg_ob = Py_True;
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &arg_ob))
            return NULL;
        pop_least_recent = PyObject_IsTrue(arg_ob);
        if (pop_least_recent == -1)
            return NULL;
    }
#endif
    LRU_ENTER_CRITICAL;

    node = pop_least_recent ? self->last : self->first;
    /* item_to_pop is new reference if not NULL */
    item_to_pop = node ? get_item(node) : NULL;
    if (item_to_pop == NULL) {
	PyErr_SetString(PyExc_KeyError, "popitem(): LRU dict is empty");
	LRU_LEAVE_CRITICAL;
	return NULL;
    }
    lru_ass_sub(self, PyTuple_GET_ITEM(item_to_pop, 0), NULL);

    LRU_LEAVE_CRITICAL;
    /* No purging necessary as this will not add new items to LRU */

    return item_to_pop;
}

static PyObject *
LRU_keys(LRU *self) {
    PyObject * result;

    LRU_ENTER_CRITICAL;
    result = collect(self, get_key);
    LRU_LEAVE_CRITICAL;

    return result;
}

static PyObject *
get_value(Node *node)
{
    Py_INCREF(node->value);
    return node->value;
}

static PyObject *
LRU_values(LRU *self)
{
    PyObject * result;

    LRU_ENTER_CRITICAL;
    result = collect(self, get_value);
    LRU_LEAVE_CRITICAL;

    return result;
}

static PyObject *
LRU_set_callback(LRU *self, PyObject *args)
{
    PyObject *result;

    LRU_ENTER_CRITICAL;
    result = set_callback(self, args);
    LRU_LEAVE_CRITICAL;

    return result;
}


static PyObject *
LRU_items(LRU *self)
{
    PyObject * result;

    LRU_ENTER_CRITICAL;
    result = collect(self, get_item);
    LRU_LEAVE_CRITICAL;

    return result;
}

static PyObject *
LRU_set_size(LRU *self, PyObject *args, PyObject *kwds)
{
    Py_ssize_t newSize;

    if (!PyArg_ParseTuple(args, "n", &newSize)) {
        return NULL;
    }
    if (newSize <= 0) {
        PyErr_SetString(PyExc_ValueError, "Size should be a positive number");
        return NULL;
    }

    LRU_ENTER_CRITICAL;

    while (lru_length(self) > newSize) {
        lru_delete_last(self);
    }
    self->size = newSize;

    LRU_LEAVE_CRITICAL;
    lru_purge_staging(self);

    Py_RETURN_NONE;
}

static PyObject *
LRU_clear(LRU *self)
{
    Node *c;

    LRU_ENTER_CRITICAL;

    c = self->first;
    while (c) {
        Node* n = c;
        c = c->next;
        lru_remove_node(self, n);
    }
    PyDict_Clear(self->dict);

    self->hits = 0;
    self->misses = 0;

    LRU_LEAVE_CRITICAL;

    Py_RETURN_NONE;
}


static PyObject *
LRU_get_size(LRU *self)
{
    PyObject *res;

    LRU_ENTER_CRITICAL;
    res = Py_BuildValue("i", self->size);
    LRU_LEAVE_CRITICAL;

    return res;
}

static PyObject *
LRU_get_stats(LRU *self)
{
    PyObject *res;

    LRU_ENTER_CRITICAL;
    res = Py_BuildValue("nn", self->hits, self->misses);
    LRU_LEAVE_CRITICAL;

    return res;
}


/* Hack to implement "key in lru" */
static PySequenceMethods lru_as_sequence = {
    0,                             /* sq_length */
    0,                             /* sq_concat */
    0,                             /* sq_repeat */
    0,                             /* sq_item */
    0,                             /* sq_slice */
    0,                             /* sq_ass_item */
    0,                             /* sq_ass_slice */
    (objobjproc) LRU_seq_contains, /* sq_contains */
    0,                             /* sq_inplace_concat */
    0,                             /* sq_inplace_repeat */
};

static PyMethodDef LRU_methods[] = {
    {"__contains__", (PyCFunction)LRU_contains_key, METH_O | METH_COEXIST,
                    PyDoc_STR("L.__contains__(key) -> Check if key is there in L")},
    {"keys", (PyCFunction)LRU_keys, METH_NOARGS,
                    PyDoc_STR("L.keys() -> list of L's keys in MRU order")},
    {"values", (PyCFunction)LRU_values, METH_NOARGS,
                    PyDoc_STR("L.values() -> list of L's values in MRU order")},
    {"items", (PyCFunction)LRU_items, METH_NOARGS,
                    PyDoc_STR("L.items() -> list of L's items (key,value) in MRU order")},
    {"has_key",	(PyCFunction)LRU_contains, METH_VARARGS,
                    PyDoc_STR("L.has_key(key) -> Check if key is there in L")},
    {"get",	(PyCFunction)LRU_get, METH_VARARGS,
                    PyDoc_STR("L.get(key, instead) -> If L has key return its value, otherwise instead")},
    {"setdefault", (PyCFunction)LRU_setdefault, METH_VARARGS,
                    PyDoc_STR("L.setdefault(key, default=None) -> If L has key return its value, otherwise insert key with a value of default and return default")},
    {"pop", (PyCFunction)LRU_pop, METH_VARARGS,
                    PyDoc_STR("L.pop(key[, default]) -> If L has key return its value and remove it from L, otherwise return default. If default is not given and key is not in L, a KeyError is raised.")},
    {"popitem", (PyCFunction)LRU_popitem, METH_VARARGS | METH_KEYWORDS,
                    PyDoc_STR("L.popitem([least_recent=True]) -> Returns and removes a (key, value) pair. The pair returned is the least-recently used if least_recent is true, or the most-recently used if false.")},
    {"set_size", (PyCFunction)LRU_set_size, METH_VARARGS,
                    PyDoc_STR("L.set_size() -> set size of LRU")},
    {"get_size", (PyCFunction)LRU_get_size, METH_NOARGS,
                    PyDoc_STR("L.get_size() -> get size of LRU")},
    {"clear", (PyCFunction)LRU_clear, METH_NOARGS,
                    PyDoc_STR("L.clear() -> clear LRU")},
    {"get_stats", (PyCFunction)LRU_get_stats, METH_NOARGS,
                    PyDoc_STR("L.get_stats() -> returns a tuple with cache hits and misses")},
    {"peek_first_item", (PyCFunction)LRU_peek_first_item, METH_NOARGS,
                    PyDoc_STR("L.peek_first_item() -> returns the MRU item (key,value) without changing key order")},
    {"peek_last_item", (PyCFunction)LRU_peek_last_item, METH_NOARGS,
                    PyDoc_STR("L.peek_last_item() -> returns the LRU item (key,value) without changing key order")},
    {"update", (PyCFunction)LRU_update, METH_VARARGS | METH_KEYWORDS,
                    PyDoc_STR("L.update() -> update value for key in LRU")},
    {"set_callback", (PyCFunction)LRU_set_callback, METH_VARARGS,
                    PyDoc_STR("L.set_callback(callback) -> set a callback to call when an item is evicted.")},
    {NULL,	NULL},
};

static PyObject*
LRU_repr(LRU* self)
{
    return PyObject_Repr(self->dict);
}

static int
LRU_init(LRU *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"size", "callback", NULL};
    PyObject *callback = NULL;
    self->callback = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "n|O", kwlist, &self->size, &callback)) {
        return -1;
    }

    if (callback && callback != Py_None) {
        if (!PyCallable_Check(callback)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return -1;
        }
        Py_XINCREF(callback);
        self->callback = callback;
    }

    if ((Py_ssize_t)self->size <= 0) {
        PyErr_SetString(PyExc_ValueError, "Size should be a positive number");
        return -1;
    }
    self->dict = PyDict_New();
    self->first = self->last = NULL;
    self->hits = 0;
    self->misses = 0;
    self->staging_list = PyList_New(0);
    self->purge_flag = 0;
    return 0;
}

static void
LRU_dealloc(LRU *self)
{
    if (self->dict) {
        LRU_clear(self);  /* Will NOT call callback on any staging elems. */
        Py_DECREF(self->dict);
    }
    /* Dispose of reference to callback */
    Py_XDECREF(self->callback);
    self->callback = NULL;
    /* Set purge flag, trigger no-callback purge (decref all list elements) */
    self->purge_flag = 1;
    lru_purge_staging(self);
    Py_XDECREF(self->staging_list);
    PyObject_Del((PyObject *)self);
}

PyDoc_STRVAR(lru_doc,
"LRU(size, callback=None) -> new LRU dict that can store up to size elements\n"
"An LRU dict behaves like a standard dict, except that it stores only fixed\n"
"set of elements. Once the size overflows, it evicts least recently used\n"
"items.  If a callback is set it will call the callback with the evicted key\n"
" and item.\n\n"
"Eg:\n"
">>> l = LRU(3)\n"
">>> for i in range(5):\n"
">>>   l[i] = str(i)\n"
">>> l.keys()\n"
"[2,3,4]\n\n"
"Note: An LRU(n) can be thought of as a dict that will have the most\n"
"recently accessed n items.\n");

static PyTypeObject LRUType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "lru.LRU",               /* tp_name */
    sizeof(LRU),             /* tp_basicsize */
    0,                       /* tp_itemsize */
    (destructor)LRU_dealloc, /* tp_dealloc */
    0,                       /* tp_print */
    0,                       /* tp_getattr */
    0,                       /* tp_setattr */
    0,                       /* tp_compare */
    (reprfunc)LRU_repr,      /* tp_repr */
    0,                       /* tp_as_number */
    &lru_as_sequence,        /* tp_as_sequence */
    &LRU_as_mapping,         /* tp_as_mapping */
    0,                       /* tp_hash */
    0,                       /* tp_call */
    0,                       /* tp_str */
    0,                       /* tp_getattro */
    0,                       /* tp_setattro */
    0,                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,      /* tp_flags */
    lru_doc,                 /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    LRU_methods,             /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    (initproc)LRU_init,      /* tp_init */
    0,                       /* tp_alloc */
    0,                       /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
  static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "lru",            /* m_name */
    lru_doc,          /* m_doc */
    -1,               /* m_size */
    NULL,             /* m_methods */
    NULL,             /* m_reload */
    NULL,             /* m_traverse */
    NULL,             /* m_clear */
    NULL,             /* m_free */
  };
#endif

static PyObject *
moduleinit(void)
{
    PyObject *m;

    NodeType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&NodeType) < 0)
        return NULL;

    LRUType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&LRUType) < 0)
        return NULL;

    #if PY_MAJOR_VERSION >= 3
        m = PyModule_Create(&moduledef);
    #else
        m = Py_InitModule3("lru", NULL, lru_doc);
    #endif

    if (m == NULL)
        return NULL;

    Py_INCREF(&NodeType);
    Py_INCREF(&LRUType);
    PyModule_AddObject(m, "LRU", (PyObject *) &LRUType);

    return m;
}

#if PY_MAJOR_VERSION < 3
    PyMODINIT_FUNC
    initlru(void)
    {
        moduleinit();
    }
#else
    PyMODINIT_FUNC
    PyInit_lru(void)
    {
        return moduleinit();
    }
#endif
