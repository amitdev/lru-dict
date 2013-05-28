#include <Python.h>

#define GET_NODE(d, key) ((Node *) (d)->ob_type->tp_as_mapping->mp_subscript((d), (key)))
#define PUT_NODE(d, key, node) (d->ob_type->tp_as_mapping->mp_ass_subscript(d, key, (PyObject *)node))

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
			Node *n = self->last;
			if (n) {
				self->last = n->prev;
				PyDict_DelItem(self->dict, n->key);
			}
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
LRU_keys(LRU *self) {
	register PyObject *v;
	v = PyList_New(lru_length(self));
	if (v == NULL)
		return NULL;

	Node *curr = self->first;
	int i = 0;

	while (curr) {
		Node *node = curr;
		Py_XINCREF(node->key);
		PyList_SET_ITEM(v, i++, node->key);
		curr = curr->next;
	}
	Py_INCREF(v);
	return v;
}

static PyMethodDef LRU_methods[] = {
		{"keys", (PyCFunction)LRU_keys, METH_NOARGS,
				PyDoc_STR("L.keys() -> list of L's keys in MRU order")},
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
		0,                       /* tp_doc */
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
	PyModule_AddObject(m, "Node", (PyObject *) &NodeType);
	PyModule_AddObject(m, "LRU", (PyObject *) &LRUType);
}
