#define IBTPTREEIMPL
#include "ibtp_tree.h"
#include "utils.h"
#undef IBTPTREEIMPL

#include <assert.h>
#include "ibtp_node.h"

void print_node(btnode* node) {
	if (node->size == 0) {
		printf("Empty Node\n");
		return;
	}
	UINT16 nbytes = ibtp_nbytes(node);
	UINT16 nkeys = ibtp_nkeys(node);
	UINT16 type = mdlUtils.binary.little_endian.uint16(node->dump);
	assert(mdlUtils.binary.little_endian.uint16(node->dump + NKEYS_OFFSET) == nkeys);
	printf("Size = %d; Type = %d; Keys = %d\n", nbytes, type, nkeys);
	for (int i = 0; i < nkeys; i++)
	{
		printf("Key %d: ", i);
		key* key = ibtp_get_key(node, i, 0);
		for (int j = 0; j < key->size; j++)
		{
			printf("%c", *(char*)(key->dump + j));
		}
		printf("\n");
		free(key);
		printf("Value %d: ", i);
		value* val = ibtp_get_val(node, i, 0);
		for (int j = 0; j < val->size; j++)
		{
			printf("%c", *(char*)(val->dump + j));
		}
		printf("\n");
		free(val);
	}
	printf("\n");
}

void print_tree(btree* tree, btnode* node, int level) {
	print_node(node);
	if (ibtp_btype(node) == 1)
		for (int i = 0; i < ibtp_nkeys(node); i++)
		{
			printf("Child %d ; level %d: \n", i + 1, level);
			btnode child = tree->get(ibtp_get_ptr(node, i));
			if (child.size == 0) printf("Empty node!\n");
			else print_tree(tree, &child, level + 1);
		}
}

#define print_node(node) 0; //print_node(node);
#define print_tree(tree, node) 0; //printf("Tree: \n"); print_tree(tree, node, 1);

/*[predefined function signeture]*/ void ibtp_node_insert(btree* tree, btnode* new, btnode* node, UINT16 idx, key* key, value* val);
/*!
*	@brief Insert a KV into a node, the result might be split into 2 nodes.
*
*	The caller is responsible for deallocating the input node and splitting and allocating result nodes.
*
*	@param[in,out] tree Tree in wich change node will be inserted. Is used to retrive pointers to chield nodes when try to insert in inner node.
*	@param[in] node Node in which key and val pair will be inserted; Can contain values and other stuff cause is retrieved from tree earlier.
*	@param[in] key Value of key of kv-pair that needs to be inserted.
*	@param[in] val Value of value of kv-pair that needs to be inserted.
*
*	@return Updated with inserted kv-pair node that has size greater than node original size so need to be splitted by caller.
*/
btnode* ibtp_tree_insert(btree* tree, btnode* node, key* key, value* val) {

	print_node(node);
	/*! @brief Result node. Formed from input node, key and value.*/
	btnode* new = dump_create_s(2 * BTREE_PAGE_SIZE);

	/* @brief Serial number of key in the node; Can be interpreted like serial number of pointer to chield node where key must/can be located.*/
	UINT16 idx = ibtp_node_lookup(node, key);

	switch (ibtp_btype(node)) {
	case BNODE_LEAF:
	{
		struct mem_dump* first_key = ibtp_get_key(node, idx, 0);
		if (!dump_cmp(key, first_key))
			ibtp_leaf_update(new, node, idx, key, val);
		else
			ibtp_leaf_insert(new, node, idx + 1, key, val);
		free(first_key);
		break;
	}
	case BNODE_NODE:
	{
		/*! @brief Behavior is same as caller function. @see @ref description of this function */
		ibtp_node_insert(tree, new, node, idx, key, val);
		break;
	}
	default:
		assert(0 && "bad node!");
	}

	print_node(new);

	return new;
}


value* ibtp_tree_get(btree* tree, btnode* node, key* key) {
	UINT16 idx = ibtp_node_lookup(node, key);
	switch (ibtp_btype(node)) {
	case BNODE_LEAF:
	{
		struct mem_dump* first_key = ibtp_get_key(node, idx, 0);
		if (!dump_cmp(key, first_key))
			return ibtp_get_val(node, idx, 1);
		else
			return (void*)0;
	}
	case BNODE_NODE:
	{
		btptr kid_node_ptr = ibtp_get_ptr(node, idx);
		btnode kid_node = tree->get(kid_node_ptr);
		return ibtp_tree_get(tree, &kid_node, key);
	}
	default:
		assert(0 && "bad node!");
		return (void*)0;
	}
}

/*[predefined function signeture]*/void ibtp_node_replace_kid_n(btree* tree, btnode* new, btnode* old, UINT16 idx, btnode kids[], int kids_count);
/*! @file ibtp.c
*
*	@brief Part of the ibtp_tree_insert(); KV insertion to an internal node.
*
*	@return None
*/
void ibtp_node_insert(btree* tree, btnode* new, btnode* old, UINT16 idx, key* key, value* val) {
	/*! @brief Massive of nodes of splited new kid node.*/
	struct mem_dump splited[3] = { 0 };

	/*! @brief Get and deallocate the kid node. */
	btptr kid_node_ptr = ibtp_get_ptr(old, idx);
	btnode kid_node = tree->get(kid_node_ptr);
	
	tree->del(kid_node_ptr);

	print_node(&kid_node);

	/*! @brief Recursive insertion to the kid node. */
	btnode* new_kid_node = ibtp_tree_insert(tree, &kid_node, key, val);

	int nsplit = ibtp_node_split_3(new_kid_node, splited);
	ibtp_node_replace_kid_n(tree, new, old, idx, splited, nsplit);
}

// split a bigger-than-allowed node into two.
// the second node always fits on a page.
void ibtp_node_split_2(btnode* left, btnode* right, btnode* old) {
	UINT16 type = ibtp_btype(old);
	UINT16 keys = ibtp_nkeys(old);
	int i;
	for (i = 0; i < keys; i++)
	{
		int size = POINTERS_OFFSET + sizeof(btptr) * (i + 1) + 2 * (i + 1)
			+ ibtp_nbytes(old) - ibtp_kv_pos(old, keys - i - 1);
		if (size > BTREE_PAGE_SIZE) break;
	}
	int idx = keys - i;
	ibtp_set_header(left, type, idx);
	ibtp_node_append_range(left, old, 0, 0, idx);
	ibtp_set_header(right, type, i);
	ibtp_node_append_range(right, old, 0, idx, i);
	print_node(left);
	print_node(right);
}

/*!
*	@brief Split node to multiple nodes and put them in array.
*
*	Split the node if it's too big. The results are 1~3 nodes.
*
*	@param[in] old Node that splited.
*	@param[out] splited Array of nodes that are result of node splitting.
*
*	@return Count of splited nodes.
*/
int ibtp_node_split_3(btnode* old, struct mem_dump splited[]) {
	//print_node(old);
	/*! @brief The node is suit to page size. */
	if (ibtp_nbytes(old) <= BTREE_PAGE_SIZE) {
		old->size = BTREE_PAGE_SIZE;
		splited[0] = *old;
		return 1;
	}
	/*! @brief Left node of splitting; Might be large and be splited later. */
	struct mem_dump left = dump_create_np_s(2 * BTREE_PAGE_SIZE);
	/*! @brief Right node of splitting; Always fits to page size. */
	struct mem_dump right = dump_create_np_s(BTREE_PAGE_SIZE);
	ibtp_node_split_2(&left, &right, old);
	/*! @brief Left node after splitting fits to page size. */
	if (ibtp_nbytes(&left) <= BTREE_PAGE_SIZE) {
		/*! @brief Correcting node size. */
		assert(left.dump = realloc(left.dump, BTREE_PAGE_SIZE));
		left.size = _msize(left.dump);
		splited[0] = left;
		splited[1] = right;
		//print_node(&left);
		//print_node(&right);
		return 2;
	}
	/*! @brief The left node is still too large. */
	/*! @brief Left result of splitting left node. */
	struct mem_dump leftleft = dump_create_np_s(BTREE_PAGE_SIZE);
	/*! @brief Right result of splitting left node. */
	struct mem_dump middle = dump_create_np_s(BTREE_PAGE_SIZE);
	ibtp_node_split_2(&leftleft, &middle, &left);
	/*! @brief Must fits one page. */
	assert(ibtp_nbytes(&leftleft) <= BTREE_PAGE_SIZE);
	splited[0] = leftleft;
	splited[1] = middle;
	splited[2] = right;
	dump_delete(&left);
	return 3;
}

/*!
*	@brief Function appends links and replaces a link in internal node with multiple links of leaf nodes.
*
*	Copy kv-pairs from old to new node herewith replacing one kv-pair from old node with multiple new kid nodes.
*
*	@param[in, out] tree Tree in which new chield nodes will be registrated.
*	@param[in, out] new Node that replaces old node so the pointers and keys are coped from last one.
*	@param[in] idx Index of kv-pair that will be replaced with new kid nodes.
*	@param[in] kids Nodes that replace old kid node with index idx.
*
*	@return None
*/
void ibtp_node_replace_kid_n(btree* tree, btnode* new, btnode* old, UINT16 idx, btnode kids[], int kids_count) {
	ibtp_set_header(new, BNODE_NODE, ibtp_nkeys(old) + kids_count - 1);
	ibtp_node_append_range(new, old, 0, 0, idx);
	for (int i = 0; i < kids_count; i++)
	{
		btnode oper_kid_node = kids[i];
		ibtp_node_append_kv(new, idx + i, tree->allocate(oper_kid_node), ibtp_get_key(&oper_kid_node, 0, 0), dump_dummy());
	}
	ibtp_node_append_range(new, old, idx + kids_count, idx + 1, ibtp_nkeys(old) - (idx + 1));
}

/*! @file ibtp
*
*	@brief Function appends links and replaces 2 links in internal node with a new links.
*
*	@param[out] new Node that replaces old node so the pointers and keys are copied from the last one.
*
*	@return None
*/
void ibtp_node_replace_2_kid(btnode* new, btnode* old, UINT16 idx, btptr merged_child, key* first_merged_child_key) {
	ibtp_set_header(new, BNODE_NODE, ibtp_nkeys(old) - 1);
	ibtp_node_append_range(new, old, 0, 0, idx);
	ibtp_node_append_kv(new, idx, merged_child, first_merged_child_key, dump_dummy());
	ibtp_node_append_range(new, old, idx + 1, idx + 2, ibtp_nkeys(old) - (idx + 2));
}

/*
* function removes a key from a leaf node
*/
void ibtp_leaf_delete(btnode* new, btnode* old, UINT16 idx) {
	ibtp_set_header(new, BNODE_LEAF, ibtp_nkeys(old) - 1);
	ibtp_node_append_range(new, old, 0, 0, idx);
	ibtp_node_append_range(new, old, idx, idx + 1, ibtp_nkeys(old) - (idx + 1));
}

/*[predefined function signeture]*/btnode* ibtp_node_delete(btree* tree, btnode* node, UINT16 idx, key* key);
/*
* function deletes a key from the tree
*/
btnode* ibtp_tree_delete(btree* tree, btnode node, key* key) {

	print_node(&node);

	// where to find the key?
	UINT16 idx = ibtp_node_lookup(&node, key);
	// act depending on the node type
	switch (ibtp_btype(&node)) {
	case BNODE_LEAF:
		if (dump_cmp(key, ibtp_get_key(&node, idx, 0))) {
			return dump_dummy(); // not found
		}
		// delete the key in the leaf
		btnode* new = dump_create_s(BTREE_PAGE_SIZE);
		ibtp_leaf_delete(new, &node, idx);

		print_node(new);

		return new;
	case BNODE_NODE:
		return ibtp_node_delete(tree, &node, idx, key);
	default:
		assert(0 && "bad node!");
	}
}


/*[predefine function signeture]*/btnode ibtp_should_merge(btree* tree, btnode* node, UINT16 idx, btnode* updated, int* status);
/*[predefine function signeture]*/void ibtp_node_merge(btnode* new, btnode* left, btnode* right);
/*
* function is part of the ibtp_tree_delete; function deletes key in internal node
*/
btnode* ibtp_node_delete(btree* tree, btnode* node, UINT16 idx, key* key) {
	// recurse into the kid
	btptr kptr = ibtp_get_ptr(node, idx);
	btnode* updated = ibtp_tree_delete(tree, tree->get(kptr), key);

	if (dump_get_size(updated) == 0) {
		return dump_dummy(); // not found
	}

	print_tree(tree, updated);

	tree->del(kptr);
	btnode* new = dump_create_s(BTREE_PAGE_SIZE);
	// check for merging
	int merge_dir = 0;
	btnode sibling = ibtp_should_merge(tree, node, idx, updated, &merge_dir);
	if (merge_dir < 0) //left
	{
		btnode merged = dump_create_np_s(BTREE_PAGE_SIZE);
		ibtp_node_merge(&merged, &sibling, updated);

		print_node(&merged);

		tree->del(ibtp_get_ptr(node, idx - 1));
		ibtp_node_replace_2_kid(new, node, idx - 1, tree->allocate(merged), ibtp_get_key(&merged, 0, 0));
	}
	else if (merge_dir > 0) //right
	{
		btnode merged = dump_create_np_s(BTREE_PAGE_SIZE);
		ibtp_node_merge(&merged, updated, &sibling);
		tree->del(ibtp_get_ptr(node, idx + 1));
		ibtp_node_replace_2_kid(new, node, idx, tree->allocate(merged), ibtp_get_key(&merged, 0, 0));
	}
	else if (merge_dir == 0) {
		//assert(ibtp_nkeys(updated) > 0);
		if (ibtp_nkeys(updated) > 0)
			ibtp_node_replace_kid_n(tree, new, node, idx, updated, 1);
		else ibtp_node_replace_kid_n(tree, new, node, idx, updated, 0);
	}

	print_tree(tree, new);

	return new;
}

/*
* function merges 2 nodes(left, right params) into 1(new param)
*/
void ibtp_node_merge(btnode* new, btnode* left, btnode* right) {
	ibtp_set_header(new, ibtp_btype(left), ibtp_nkeys(left) + ibtp_nkeys(right));
	ibtp_node_append_range(new, left, 0, 0, ibtp_nkeys(left));
	ibtp_node_append_range(new, right, ibtp_nkeys(left), 0, ibtp_nkeys(right));
}

/*
* function determines if should the updated kid be merged with a sibling
* [return] (0 - don't merge; -1 - merge with left; +1 - merge with right)
* [return] sibling - pointer to the pointer to merging sibling node
*/
btnode ibtp_should_merge(btree* tree, btnode* node, UINT16 idx, btnode* updated, int* status) {
	/*if (ibtp_nbytes(updated) > BTREE_PAGE_SIZE / 4) {
		*status = 0;
		return *dump_dummy();
	}*/
	print_node(node);
	print_node(updated);
	if (idx > 0) {
		btnode result = tree->get(ibtp_get_ptr(node, idx - 1));
		UINT16 merged = ibtp_nbytes(&result) + ibtp_nbytes(updated) - POINTERS_OFFSET;
		if (merged <= BTREE_PAGE_SIZE) {
			*status = -1;
			return result;
		}
	}
	if (idx + 1 < ibtp_nkeys(node)) {
		btnode result = tree->get(ibtp_get_ptr(node, idx + 1));
		UINT16 merged = ibtp_nbytes(&result) + ibtp_nbytes(updated) - POINTERS_OFFSET;
		if (merged <= BTREE_PAGE_SIZE) {
			*status = +1;
			return result;
		}
	}
	*status = 0;
	return *dump_dummy();
}

/*
* function delete from interface
*/
boolean Delete(btree* tree, key* key) {
	assert(dump_get_size(key) != 0);
	assert(dump_get_size(key) <= BTREE_MAX_KEY_SIZE);
	if (tree->root == 0) {
		return 0;
	}
	btnode* updated = ibtp_tree_delete(tree, tree->get(tree->root), key);
	if (dump_get_size(updated) == 0) {
		return 0; // not found
	}
	btnode old_root = tree->get(tree->root);
	print_tree(tree, &old_root);
	tree->del(tree->root);

	print_tree(tree, updated);

	if ((ibtp_btype(updated) == BNODE_NODE) && (ibtp_nkeys(updated) == 1)) {
		tree->root = ibtp_get_ptr(updated, 0); // remove a level
	}
	else {
		tree->root = tree->allocate(*updated);
	}
	btnode root = tree->get(tree->root);
	print_tree(tree, &root);
	return 1;
}

value* TreeGet(btree* tree, key* key) {
	assert(dump_get_size(key) != 0);
	assert(dump_get_size(key) <= BTREE_MAX_KEY_SIZE);
	btptr root_ptr = tree->root;
	btnode root = tree->get(root_ptr);
	return ibtp_tree_get(tree, &root, key);
}

/*
* function insert from interface
*/
void Insert(btree* tree, key* key, value* val) {

	assert(dump_get_size(key) != 0);
	assert(dump_get_size(key) <= BTREE_MAX_KEY_SIZE);
	assert(dump_get_size(val) <= BTREE_MAX_VAL_SIZE);

	if (tree->root == 0)
	{
		/*! @brief The first node. */
		btnode root = dump_create_np_s(BTREE_PAGE_SIZE);
		/*! @brief A dummy key, this makes the tree cover the whole key space thus a lookup can always find a containing node. */
		struct mem_dump* dummy = dump_dummy();

		ibtp_set_header(&root, BNODE_LEAF, 2);
		ibtp_node_append_kv(&root, 0, 0, dummy, dummy);
		ibtp_node_append_kv(&root, 1, 0, key, val);
		tree->root = tree->allocate(root);

	}
	else
	{
		btptr root_ptr = 0;
		btnode root = { 0 };
		btnode* new_root = (void*)0;
		btnode splited_new_root = { 0 };
		struct mem_dump splited[3] = { 0 };
		int nsplit = 0;
		value* empty_value_for_inner_node = dump_dummy();

		root_ptr = tree->root;
		root = tree->get(root_ptr);
		tree->del(root_ptr);

		//print_node(&root);

		new_root = ibtp_tree_insert(tree, &root, key, val);

		nsplit = ibtp_node_split_3(new_root, &splited);

		for (int i = 0; i < nsplit; i++)
		{
			print_node(&splited[i]);
		}

		if (nsplit > 1) {
			splited_new_root = dump_create_np_s(BTREE_PAGE_SIZE);
			ibtp_set_header(&splited_new_root, BNODE_NODE, nsplit);
			for (int i = 0; i < nsplit; i++)
			{
				btnode oper_child_node = splited[i];
				btptr ptr_to_new_chield_node = tree->allocate(oper_child_node);
				struct mem_dump* first_key_of_chield_node = ibtp_get_key(&oper_child_node, 0, 0);
				ibtp_node_append_kv(&splited_new_root, i, ptr_to_new_chield_node, first_key_of_chield_node, empty_value_for_inner_node);
			}
			tree->root = tree->allocate(splited_new_root);
			print_node(&splited_new_root);
			UINT16 size = ibtp_nbytes(&splited_new_root);
			dump_delete(new_root);
		}
		else
			tree->root = tree->allocate(*new_root);
	}

}