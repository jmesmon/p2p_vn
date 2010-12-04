#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>

struct node{
	void *data;
	struct node *prev;
	struct node *next;
};
typedef struct node *node_ptr;

struct que{
	pthread_mutex_t mut;
	node_ptr head;
	node_ptr tail;
	};
typedef struct que *que_ptr;

que_ptr create() {
	que_ptr q;
	q = malloc(sizeof(struct que));
	q->head = NULL;
	q->tail = NULL;
	int t = pthread_mutex_init(&q->mut, NULL);

	return q;
}


int push(que_ptr q, void *obj) {
	node_ptr new_node;
	new_node = malloc(sizeof(struct node));
	
	/* malloc failure */
	if (new_node == NULL){
		return -1;
	}
	
	new_node->data = obj;
	pthread_mutex_lock(&q->mut);
	if(q->head == NULL){/*empty list*/
		q->head = new_node;
		q->tail = new_node;
		new_node->prev = NULL;
		new_node->next = NULL;
		pthread_mutex_unlock(&q->mut);
		return 1;
	}
	new_node->next = q->head;
	q->head->prev = new_node;
	q->head = new_node;
	pthread_mutex_unlock(&q->mut);
	return 1;
}

void* pop(que_ptr q) {

	if(q->head == NULL) {
		/*no nodes in que */
		return NULL;
	}

	void* data = q->tail->data;	
	node_ptr tmp;
	pthread_mutex_lock(&q->mut);
	
	if(q->head->next == NULL) {
		tmp = q->head;
		q->head = NULL;
		q->tail = NULL;
		free(tmp);
		pthread_mutex_unlock(&q->mut);
		return data;
	}
	
	tmp = q->tail;
	q->tail = q->tail->prev;
	q->tail->next = NULL;
	free(tmp);
	pthread_mutex_unlock(&q->mut);
	return data;
}

//returns head of linked list for now
node_ptr pop_all(que_ptr q) {
	if(q->head == NULL) {
		return NULL;
	}
	pthread_mutex_lock(&q->mut);
	node_ptr tmp = q->head;
	q->head = NULL;
	q->tail = NULL;
	pthread_mutex_unlock(&q->mut);
	return tmp;
}


void print_que(que_ptr q){
	node_ptr n;
	
	n = q->head;
	while(n != NULL) {
		printf("%s\n", n->data);
		n=n->next;
	}
	free(n);
}

void destroy(que_ptr q) {
	//can mutex_destroy a locked mutex?
	pthread_mutex_lock(&q->mut);
	if(q->head == NULL) {
		free(q);
		pthread_mutex_destroy(&q->mut);
		return; }
	
	node_ptr tmp = q->tail;
	node_ptr tmp2;
	while(tmp != NULL) {
		tmp2 = tmp;
		tmp = tmp->prev;
		free(tmp2);
	}
	pthread_mutex_destroy(&q->mut);
	free(q);
}

int main() {

	que_ptr q;
	q = create();
	node_ptr g;
	//push test
	push(q, "1");
	//push(q, "2222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222");
	//push(q, "3123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312343123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123123434123431233123412343412343123312341234341234312331234123434123431233123412343412343123");
	push(q, "3");
	push(q, "3");
	push(q, "3");
	push(q, "3");
	push(q, "5");
	push(q, g);
//	print_que(q);
	
	//pop_all test
	node_ptr b;
	b = pop_all(q);
	while(b != NULL) {
		printf("\n%s\n", b->data);
		b = b->next;
		}
	//pop test
	
	push(q, "a");
	push(q, "b");
	push(q, "c");
	void* d;
	d = pop(q);
	printf("\n%s\n", d);
	d = pop(q);
	printf("\n%s\n", d);
	d = pop(q);
	printf("\n%s\n", d);
	d = pop(q);
	d = pop(q);
	d = pop(q);
	d = pop(q);
	d = pop(q);
	d = pop(q);
	d = pop(q);

//	free(q);
	destroy(q);
	//printf("\n%s\n", d);
	return 0;
}
