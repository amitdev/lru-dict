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
 */
#define GET_NODE(d, key) (Node *) (d)->ob_type->tp_as_mapping->mp_subscript((d), (key))
#define PUT_NODE(d, key, node) (d)->ob_type->tp_as_mapping->mp_ass_subscript((d), (key), ((PyObject *)node))
#define OBJ_INCRFED(obj) Py_INCREF(obj), obj

typedef struct _Node {
	PyObject_HEAD
	PyObject * value;
	PyObject * key;
	struct _Node * prev;
	struct _Node * next;
} Node;

static void
node_remove(Node* self)
{
	if (self->prev) {
		self->prev->next = self->next;
	}
	if (self->next) {
		self->next->prev = self->prev;
	}
	self->next = self->prev = NULL;
}

static void
node_dealloc(Node* self)
{
	Py_XDECREF(self->value);
	node_remove(self);
	PyObject_Del((PyObject*)self);
}

static PyObject*
node_repr(Node* self)
{
	return PyObject_Repr(self->value);
}

static PyTypeObject NodeType = {
		PyObject_HEAD_INIT(NULL)
		0,                       /* ob_size */
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
} LRU;

static Py_ssize_t
lru_length(LRU *self)
{
	return PyDict_Size(self->dict);
}

static PyObject *
LRU_contains(LRU *self, PyObject *args)
{
	PyObject *key;
	if (!PyArg_ParseTuple(args, "O", &key))
		return NULL;
	if (PyDict_Contains(self->dict, key))
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}

static PyObject *
lru_subscript(LRU *self, register PyObject *key)
{
	Node *node = GET_NODE(self->dict, key);
	if (node != NULL && PyObject_TypeCheck(node, &NodeType)) {
		if (self->first != self->last) {
			node_remove(node);
			node->next = self->first;
			self->first->prev = node;
			self->first = node;
		}
		Py_INCREF(node->value);
		return node->value;
	}
	return (PyObject *) node;
}

static PyObject *
LRU_get(LRU *self, PyObject *args)
{
	PyObject *key;
	PyObject *instead;

	if (!PyArg_ParseTuple(args, "OO", &key, &instead))
		return NULL;
	if (PyDict_Contains(self->dict, key))
		return lru_subscript(self, key);
	return (OBJ_INCRFED(instead));
}

void delete_last(LRU* self) {
	Node* n = self->last;
	if (n) {
		self->last = n->prev;
		if (self->last == NULL ) {
			self->first = NULL;
		}
		PyDict_DelItem(self->dict, n->key);
	}
}

static int
lru_ass_sub(LRU *self, PyObject *key, PyObject *value)
{
	int res = 0;
	Node *node = NULL;

	if (value) {
		/* Going to add/update self->dict, so adjust lru list */
		node = PyObject_NEW(Node, &NodeType);
		node->key = key;
		node->value = value;
		node->next = node->prev = NULL;

		Py_XINCREF(value);

		if (lru_length(self) == self->size) {
			delete_last(self);
		}

		if (self->first != NULL) {
			self->first->prev = node;
			node->next = self->first;
		}
		self->first = node;
		if (self->last == NULL) {
			self->last = self->first;
		}
	} else {
		/* Deleting an entry. Adjust first/last properly */
		Node *n = GET_NODE(self->dict, key);
		if (n != NULL && PyObject_TypeCheck(n, &NodeType)) {
			if (n == self->first) {
				self->first = n->next;
				if (self->first)
					self->first->prev = NULL;
			}
			if (n == self->last) {
				self->last = n->prev;
				if (self->last)
					self->last->next = NULL;
			}
		}
		Py_XDECREF(n);
	}
	res = PUT_NODE(self->dict, key, node);
	Py_XDECREF(node);
	return res;
}

static PyMappingMethods LRU_as_mapping = {
		(lenfunc)lru_length,        /*mp_length*/
		(binaryfunc)lru_subscript,  /*mp_subscript*/
		(objobjargproc)lru_ass_sub, /*mp_ass_subscript*/
};

static PyObject *
collect(LRU *self, PyObject * (*getterfunc)(Node *)) {
	register PyObject *v;
	v = PyList_New(lru_length(self));
	if (v == NULL)
		return NULL;
	Node *curr = self->first;
	int i = 0;

	while (curr) {
		Node *node = curr;
		PyList_SET_ITEM(v, i++, getterfunc(node));
		curr = curr->next;
	}
	return v;
}

static PyObject *
get_key(Node *node) {
	return OBJ_INCRFED(node->key);
}

static PyObject *
LRU_keys(LRU *self) {
	return collect(self, get_key);
}

static PyObject *
get_value(Node *node) {
	return OBJ_INCRFED(node->value);
}

static PyObject *
LRU_values(LRU *self) {
	return collect(self, get_value);
}

static PyObject *
get_item(Node *node) {
	PyObject *tuple = PyTuple_New(2);
	PyTuple_SET_ITEM(tuple, 0, (OBJ_INCRFED(node->key)));
	PyTuple_SET_ITEM(tuple, 1, (OBJ_INCRFED(node->value)));
	return tuple;
}
static PyObject *
LRU_items(LRU *self) {
	return collect(self, get_item);
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
    while (lru_length(self) > newSize) {
        //delete obsolete items
        Node *n = self->last;
        if (n) {
            self->last = n->prev;
            if (self->last == NULL) {
                self->first = NULL;
            }
            PyDict_DelItem(self->dict, n->key);
        }
    }
    self->size = newSize;
    Py_INCREF(Py_None);
    return (PyObject *) Py_None;
}

static PyObject *
LRU_get_size(LRU *self)
{
    return Py_BuildValue("i", self->size);
}


static PyMethodDef LRU_methods[] = {
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
                {"set_size", (PyCFunction)LRU_set_size, METH_VARARGS,
								PyDoc_STR("L.set_size() -> set size of LRU")},
        		{"get_size", (PyCFunction)LRU_get_size, METH_NOARGS,
								PyDoc_STR("L.get_size() -> get size of LRU")},
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
	if (!PyArg_ParseTuple(args, "n", &self->size)) {
		return -1;
	}
	if ((Py_ssize_t)self->size <= 0) {
		PyErr_SetString(PyExc_ValueError, "Size should be a positive number");
		return -1;
	}
	self->dict = PyDict_New();
	self->first = self->last = NULL;
	return 0;
}

static void
LRU_dealloc(LRU *self)
{
	Py_XDECREF(self->dict);
	PyObject_Del((PyObject*)self);
}

PyDoc_STRVAR(lru_doc,
"LRU(size) -> new LRU dict that can store upto size elements\n"
"An LRU dict behaves like a standard dict, except that it stores only fixed\n"
"set of elements. Once the size overflows, it evicts least recently used items.\n\n"
"Eg:\n"
">>> l = LRU(3)\n"
">>> for i in range(5):\n"
">>>   l[i] = str(i)\n"
">>> l.keys()\n"
"[2,3,4]\n\n"
"Note: An LRU(n) can be thought of as a dict that will have the most recently accessed n items.\n");

static PyTypeObject LRUType = {
		PyObject_HEAD_INIT(NULL)
		0,                       /* ob_size */
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
		0,                       /* tp_as_sequence */
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

PyMODINIT_FUNC
initlru(void)
{
	PyObject *m;

	NodeType.tp_new = PyType_GenericNew;
	if (PyType_Ready(&NodeType) < 0)
		return;

	LRUType.tp_new = PyType_GenericNew;
	if (PyType_Ready(&LRUType) < 0)
		return;

	m = Py_InitModule3("lru", NULL, "lru module");
	if (m == NULL)
		return;

	Py_INCREF(&NodeType);
	Py_INCREF(&LRUType);
	PyModule_AddObject(m, "LRU", (PyObject *) &LRUType);
}
