#include "scanner_button_model.h"
#include <assert.h>
#include <stdio.h>

static scanner_button_event_t step(scanner_button_model_t *b,int64_t us,bool down,bool awake,bool idle)
{ return scanner_button_step(b,us,down,awake,idle); }

int main(void)
{
    scanner_button_model_t b={0};
    assert(step(&b,0,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3000000,true,true,true)==SCANNER_BUTTON_NONE); /* boot held */
    assert(step(&b,3000010,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3030010,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3100000,true,false,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3129999,true,false,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3130000,true,false,true)==SCANNER_BUTTON_WAKE);
    assert(step(&b,6000000,true,true,true)==SCANNER_BUTTON_NONE); /* whole gesture consumed */
    assert(step(&b,6100000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,6130000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,6200000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,6230000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,8229999,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,8230000,true,true,true)==SCANNER_BUTTON_HOLD);
    assert(step(&b,12000000,true,true,true)==SCANNER_BUTTON_NONE);
    /* A gesture that begins busy stays consumed when idle returns. */
    assert(step(&b,12100000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,12130000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,12200000,true,true,false)==SCANNER_BUTTON_NONE);
    assert(step(&b,12230000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,16000000,true,true,true)==SCANNER_BUTTON_NONE);
    /* Bounce cannot satisfy startup release or create a second action. */
    b=(scanner_button_model_t){0};
    assert(step(&b,0,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,20000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3000000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3100000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3130000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3200000,true,false,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3210000,false,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3220000,true,true,true)==SCANNER_BUTTON_NONE);
    assert(step(&b,3250000,true,true,true)==SCANNER_BUTTON_WAKE);
    assert(step(&b,6000000,true,true,true)==SCANNER_BUTTON_NONE);
    puts("BOOT button model passed");
}
