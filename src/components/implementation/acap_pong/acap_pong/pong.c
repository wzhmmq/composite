#include <cos_component.h>
#include <print.h>
#include <acap_pong.h>

#include <acap_mgr.h>
#include <cos_alloc.h>
#include <sched.h>

//volatile int f;
//void call(void) { f = *(int*)NULL; return; }
int call(int a, int b, int c, int d) { 
	printc("doing call in pong with params %d %d %d %d\n", a,b,c,d);
	return a+b+c+d; 
}



/////////////////// move to lib later
struct __cos_ainv_srv_thd {
	int acap;
	int cli_ncaps;
	vaddr_t *fn_mapping;
	volatile int stop;

	void *shared_page;
	struct shared_struct shared_struct;
} CACHE_ALIGNED;

struct __cos_ainv_srv_thd *__cos_ainv_thds[MAX_NUM_THREADS]; 

static inline int exec_fn(int (*fn)(), int nparams, int *params) {
	int ret;

	assert(fn);

	switch (nparams)
	{		
	case 0:
		ret = fn();
		break;
	case 1:
		ret = fn(params[0]);
		break;
	case 2:
		ret = fn(params[0], params[1]);
		break;
	case 3:
		ret = fn(params[0], params[1], params[2]);
		break;
	case 4:
		ret = fn(params[0], params[1], params[2], params[3]);
		break;
	}
	
	return ret;
}

int cos_ainv_handling(void) {
	struct __cos_ainv_srv_thd *curr;
	int acap, i;
	int curr_thd_id = cos_get_thd_id();

	__cos_ainv_thds[curr_thd_id] = malloc(sizeof(struct __cos_ainv_srv_thd));
	curr = __cos_ainv_thds[curr_thd_id];
	if (unlikely(curr == NULL)) goto err_nomem;

	printc("upcall thread %d (core %ld) waiting...\n", cos_get_thd_id(), cos_cpuid());
	sched_block(cos_spd_id(), 0);
	printc("upcall thread %d (core %ld) up!\n", cos_get_thd_id(), cos_cpuid());
		
	curr->acap = acap_srv_lookup(cos_spd_id());
	curr->cli_ncaps = acap_srv_ncaps(cos_spd_id());
	curr->shared_page = acap_srv_lookup_ring(cos_spd_id());
	assert(curr->acap && curr->cli_ncaps && curr->shared_page);

	init_shared_page(&curr->shared_struct, curr->shared_page);

	curr->fn_mapping = malloc(sizeof(vaddr_t) * curr->cli_ncaps);
	if (unlikely(curr->fn_mapping == NULL)) goto err_nomem;
	for (i = 0; i < curr->cli_ncaps; i++) {
		curr->fn_mapping[i] = (vaddr_t)acap_srv_fn_mapping(cos_spd_id(), i);
	}
	
	assert(curr);
	acap = curr->acap;

	printc("server %ld, upcall thd %d has acap %d.\n", 
	       cos_spd_id(), curr_thd_id, acap);

	struct shared_struct *shared_struct = &curr->shared_struct;
	CK_RING_INSTANCE(inv_ring) *ring = shared_struct->ring;
	assert(ring);

	struct inv_data inv;
	while (curr->stop == 0) {
		CLEAR_SERVER_ACTIVE(shared_struct);
		if (CK_RING_DEQUEUE_SPSC(inv_ring, ring, &inv) == false) {
			//ainv_wait(acap);
		} else {
			SET_SERVER_ACTIVE(shared_struct);
			*shared_struct->server_active = 1; /**/
			printc("core %ld: got inv for cap %d, param %d, %d, %d, %d\n",
			       cos_cpuid(), inv.cap, inv.params[0], inv.params[1], inv.params[2], inv.params[3]);
			if (unlikely(inv.cap > curr->cli_ncaps || !curr->fn_mapping[inv.cap])) {
				printc("Server thread %d in comp %ld: receiving invalid cap %d\n",
				       cos_get_thd_id(), cos_spd_id(), inv.cap);
			} else {
				assert(curr->fn_mapping[inv.cap]);
				//execute!
				exec_fn((void *)curr->fn_mapping[inv.cap], 4, inv.params);
				// and write to the return value.
			}
		}
	}

	return 0;
err_nomem:
	printc("couldn't allocate memory in spd %ld\n", cos_spd_id());
	return -1;
}

void cos_upcall_fn(upcall_type_t t, void *arg1, void *arg2, void *arg3)
{
	switch (t) {
	case COS_UPCALL_AINV_HANDLER:
	{
		cos_ainv_handling();
		break;
	}
	/* case COS_UPCALL_BRAND_EXEC: */
	/* { */
	/* 	cos_upcall_exec(arg1); */
	/* 	break; */
	/* } */
	case COS_UPCALL_BOOTSTRAP:
	{
		cos_ainv_handling();
		break;
	}
	default:
		/* fault! */
		//*(int*)NULL = 0;
		printc("\n upcall type t %d\n", t);
		return;
	}
	return;
}
