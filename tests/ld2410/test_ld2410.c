/* Compile production driver directly: UART, clock and FreeRTOS are host doubles. */
#include "../../main/src/ld2410_radar.c"
#include <assert.h>
#include <stdlib.h>

#if LD2410_UART_SELFTEST_ENABLED
static const uint8_t enter_ack[] = {0xfd,0xfc,0xfb,0xfa,8,0,0xff,1,0,0,1,0,0x40,0,4,3,2,1};
static const uint8_t version_ack[] = {0xfd,0xfc,0xfb,0xfa,12,0,0xa0,1,0,0,1,0,2,1,0x16,0x24,6,0x22,4,3,2,1};
static const uint8_t exit_ack[] = {0xfd,0xfc,0xfb,0xfa,4,0,0xfe,1,0,0,4,3,2,1};
#endif
static const uint8_t measurement[] = {0xf4,0xf3,0xf2,0xf1,13,0,2,0xaa,3,0x34,0x12,50,0x78,0x56,60,0,0,0x55,0,0xf8,0xf7,0xf6,0xf5};
static void reset(void) {
    s_test_state=UART_TEST_IDLE; s_test_retries=0; s_test_tx_attempts=0; s_test_tx_ok=true;
    s_test_enter_ack=s_test_version_ack=s_test_exit_ack=s_test_reported=false;
    s_latest_pending=s_normal_frame_seen=false; s_rx_stream_len=0;
    s_parser_errors=s_timeouts=0; writes=0; clock_us=1;write_result=-99;
    s_initialized=s_uart_initialized=false; api_error=0;task_ok=1;
    s_tx_history_count=s_echo_frames=s_unknown_frames=0;
    s_uart_overflows=s_uart_errors=0;s_total_rx_bytes=0;s_uart_events=NULL;
    queued_rx_len=event_count=event_index=0;captured_len=hex_bytes=0;captured_logs[0]=0;
#if LD2410_UART_DIAGNOSTICS_ENABLED
    s_diag_started=false;s_diag_rx_dumped=s_diag_frames_logged=0;
#endif
}
static void feed(const uint8_t *p,size_t n) {
    assert(n<=sizeof(s_rx_stream)-s_rx_stream_len);
    memcpy(s_rx_stream+s_rx_stream_len,p,n);s_rx_stream_len+=n;
    ld2410_process_stream();
}
#if LD2410_UART_SELFTEST_ENABLED
static void encoder(void) {
    static const uint8_t en[]={0xfd,0xfc,0xfb,0xfa,4,0,0xff,0,1,0,4,3,2,1};
    static const uint8_t ve[]={0xfd,0xfc,0xfb,0xfa,2,0,0xa0,0,4,3,2,1};
    static const uint8_t ex[]={0xfd,0xfc,0xfb,0xfa,2,0,0xfe,0,4,3,2,1};
    reset();ld2410_selftest_step();assert(tx_size==sizeof en && !memcmp(tx,en,sizeof en));
    s_test_state=UART_TEST_VERSION_SEND;ld2410_selftest_step();assert(tx_size==sizeof ve && !memcmp(tx,ve,sizeof ve));
    s_test_state=UART_TEST_EXIT_SEND;ld2410_selftest_step();assert(tx_size==sizeof ex && !memcmp(tx,ex,sizeof ex));
    write_result=0;s_test_state=UART_TEST_EXIT_SEND;ld2410_selftest_step();assert(!s_test_tx_ok);
}
static void success_and_splits(void) {
    for(size_t split=0;split<=sizeof enter_ack;split++) {
        reset();ld2410_selftest_step();feed(enter_ack,split);feed(enter_ack+split,sizeof enter_ack-split);
        assert(s_test_enter_ack && s_test_state==UART_TEST_VERSION_SEND);
    }
    for(size_t split=0;split<=sizeof version_ack;split++) {
        reset();s_test_state=UART_TEST_VERSION_WAIT;
        feed(version_ack,split);feed(version_ack+split,sizeof version_ack-split);
        assert(s_test_version_ack && !strcmp(s_test_version,"type=1 V1.02.22062416"));
    }
    reset();ld2410_selftest_step();feed(enter_ack,sizeof enter_ack);ld2410_selftest_step();
    feed(version_ack,sizeof version_ack);ld2410_selftest_step();feed(exit_ack,sizeof exit_ack);
    assert(s_test_state==UART_TEST_DONE && s_test_enter_ack && s_test_version_ack && s_test_exit_ack);
    size_t before=writes;for(int i=0;i<100;i++)ld2410_selftest_step();assert(writes==before);
    /* V1.09 example has 00 01 firmware type: preserve LE (256), don't shift ACK fields. */
    uint8_t example[sizeof version_ack];memcpy(example,version_ack,sizeof example);example[10]=0;example[11]=1;
    reset();s_test_state=UART_TEST_VERSION_WAIT;feed(example,sizeof example);
    assert(s_test_version_ack && !strcmp(s_test_version,"type=256 V1.02.22062416"));
}
static void malformed_and_nack(void) {
    for(int state=UART_TEST_ENTER_WAIT;state<=UART_TEST_EXIT_WAIT;state+=2) {
        reset();s_test_state=(uart_test_state_t)state;
        const uint8_t *src=state==UART_TEST_ENTER_WAIT?enter_ack:state==UART_TEST_VERSION_WAIT?version_ack:exit_ack;
        size_t n=state==UART_TEST_ENTER_WAIT?sizeof enter_ack:state==UART_TEST_VERSION_WAIT?sizeof version_ack:sizeof exit_ack;
        uint8_t buf[32];memcpy(buf,src,n);buf[8]=1;feed(buf,n);
        assert(s_test_state==(state==UART_TEST_EXIT_WAIT?UART_TEST_DONE:UART_TEST_EXIT_SEND));
    }
    reset();s_test_state=UART_TEST_VERSION_WAIT;feed(exit_ack,sizeof exit_ack);assert(s_test_state==UART_TEST_VERSION_WAIT);
    uint8_t b[sizeof version_ack];memcpy(b,version_ack,sizeof b);b[sizeof b-1]=9;feed(b,sizeof b);
    assert(!s_test_version_ack && s_parser_errors==1);feed(version_ack,sizeof version_ack);assert(s_test_version_ack);
    reset();s_test_state=UART_TEST_VERSION_WAIT;uint8_t short_ack[sizeof exit_ack];memcpy(short_ack,exit_ack,sizeof short_ack);short_ack[6]=0xa0;
    feed(short_ack,sizeof short_ack);assert(!s_test_version_ack && s_test_state==UART_TEST_EXIT_SEND);
    reset();uint8_t bad[]={0xfd,0xfc,0xfb,0xfa,0xff,0xff};feed(bad,sizeof bad);assert(s_parser_errors==1);
}
static void interleaved(void) {
    reset();ld2410_selftest_step();
    uint8_t batch[128];size_t n=0;
    memcpy(batch+n,measurement,sizeof measurement);n+=sizeof measurement;
    memcpy(batch+n,enter_ack,sizeof enter_ack);n+=sizeof enter_ack;
    memcpy(batch+n,measurement,sizeof measurement);n+=sizeof measurement;
    feed(batch,n);assert(s_test_enter_ack && s_normal_frame_seen && s_rx_stream_len==0);
    for(size_t split=0;split<sizeof measurement;split++) {
        reset();feed(measurement,split);assert(!s_latest_pending);feed(measurement+split,sizeof measurement-split);assert(s_latest_pending);
    }
    reset();s_initialized=true;s_test_state=UART_TEST_EXIT_WAIT;feed(exit_ack,sizeof exit_ack);assert(!s_latest_pending);
    for(out_level=0;out_level<2;out_level++) {
        int presence=-1;bool valid=false,uart=true;
        assert(ld2410_radar_read(&presence,NULL,NULL,NULL,NULL,NULL,NULL,&valid,&uart,NULL)==ESP_OK);
        assert(presence==out_level && valid && !uart);
    }
    feed(measurement,sizeof measurement);uint16_t md,sd;bool uart=false;
    ld2410_radar_read(NULL,NULL,NULL,&md,NULL,&sd,NULL,NULL,&uart,NULL);
    assert(uart && md==0x1234 && sd==0x5678);
}
static void adversarial_streams(void) {
    /* Engineering measurement includes ACK-like bytes in its payload. */
    uint8_t eng[45]={0xf4,0xf3,0xf2,0xf1,35,0,1,0xaa,3};
    memcpy(eng+19,exit_ack,sizeof exit_ack);
    eng[39]=0x55;eng[40]=0;eng[41]=0xf8;eng[42]=0xf7;eng[43]=0xf6;eng[44]=0xf5;
    for(size_t split=0;split<=sizeof eng;split++) {
        reset();s_test_state=UART_TEST_EXIT_WAIT;feed(eng,split);feed(eng+split,sizeof eng-split);
        assert(s_latest_pending && !s_test_exit_ack && s_test_state==UART_TEST_EXIT_WAIT);
    }
    /* Malformed type, target, length, internal markers and footer. */
    const size_t fields[]={4,6,7,8,17,18,19,22};
    for(size_t i=0;i<sizeof fields/sizeof fields[0];i++) {
        reset();uint8_t b[sizeof measurement];memcpy(b,measurement,sizeof b);b[fields[i]]=0xff;
        feed(b,sizeof b);assert(!s_latest_pending && s_parser_errors>0);
        feed(measurement,sizeof measurement);assert(s_latest_pending);
    }
    /* Hostile noise remains bounded, then recovers to a valid frame. */
    reset();uint32_t seed=12345;
    for(size_t i=0;i<100000;i++) {
        seed=seed*1664525u+1013904223u;uint8_t byte=(uint8_t)(seed>>24);feed(&byte,1);
        assert(s_rx_stream_len<74);
    }
    feed(measurement,sizeof measurement);assert(s_latest_pending);
    /* A delayed safety loop cannot publish an old measurement as fresh. */
    s_initialized=true;clock_us+=3000000;bool uart=true;
    ld2410_radar_read(NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,&uart,NULL);assert(!uart);
    reset();uint8_t params[2]={1,0};
    ld2410_selftest_send(0x00a1,NULL,0);assert(writes==0);
    ld2410_selftest_send(0x00ff,params,99);assert(writes==0);
}
static void RX_EXACT_TX_FRAME_MUST_NOT_BE_ACCEPTED_AS_ACK(void) {
    /* Each of the three TX frames must be consumed once, even at every split. */
    const uart_test_state_t send[]={UART_TEST_ENTER_SEND,UART_TEST_VERSION_SEND,UART_TEST_EXIT_SEND};
    const uart_test_state_t wait[]={UART_TEST_ENTER_WAIT,UART_TEST_VERSION_WAIT,UART_TEST_EXIT_WAIT};
    for(size_t c=0;c<3;c++) for(size_t split=0;split<15;split++) {
        reset();ld2410_diag_begin();s_test_state=send[c];ld2410_selftest_step();
        uint8_t copy[16];size_t n=tx_size;memcpy(copy,tx,n);size_t cut=split<n?split:n;
        feed(copy,cut);feed(copy+cut,n-cut);
        assert(s_test_state==wait[c] && !s_test_enter_ack && !s_test_version_ack && !s_test_exit_ack);
        assert(s_echo_frames==1 && s_parser_errors==0 && s_rx_stream_len==0 && !s_latest_pending);
#if LD2410_UART_DIAGNOSTICS_ENABLED
        assert(strstr(captured_logs,"classification=COMMAND_ECHO"));
#endif
        assert(!strstr(captured_logs,"ACK candidate"));
        ld2410_process_stream();assert(s_echo_frames==1);
    }
    /* Wrong-command input is UNKNOWN without evidence of matching prior TX. */
    reset();uint8_t command[]={0xfd,0xfc,0xfb,0xfa,4,0,0xff,0,1,0,4,3,2,1};
    s_test_state=UART_TEST_ENTER_WAIT;feed(command,sizeof command);
    assert(s_unknown_frames==1 && !s_test_enter_ack && s_echo_frames==0);
    reset();uint8_t missing_status[]={0xfd,0xfc,0xfb,0xfa,2,0,0xff,1,4,3,2,1};
    s_test_state=UART_TEST_ENTER_WAIT;feed(missing_status,sizeof missing_status);
    assert(s_parser_errors==1 && !s_test_enter_ack);
    /* Echo followed by a real ACK in one batch: only the ACK advances state. */
    reset();ld2410_selftest_step();uint8_t batch[64];size_t n=tx_size;
    memcpy(batch,tx,n);memcpy(batch+n,enter_ack,sizeof enter_ack);feed(batch,n+sizeof enter_ack);
    assert(s_echo_frames==1 && s_test_enter_ack);
}
static void runtime_rx_and_diagnostics(void) {
    reset();ld2410_uart_poll_once();assert(s_test_state==UART_TEST_ENTER_WAIT);
    /* ACK already in RX ring when scheduling resumes after the timeout deadline. */
    clock_us=s_test_deadline_us+1;memcpy(queued_rx,enter_ack,sizeof enter_ack);queued_rx_len=sizeof enter_ack;
    ld2410_uart_poll_once();assert(s_test_enter_ack && s_timeouts==0 && s_test_state==UART_TEST_VERSION_WAIT);
    /* Event queue contains metadata; does not consume bytes or discard valid ACKs. */
    s_uart_events=(void *)1;queued_events[0].type=UART_FIFO_OVF;queued_events[1].type=UART_BUFFER_FULL;
    queued_events[2].type=UART_FRAME_ERR;event_count=3;
    s_rx_stream_len=3;memcpy(queued_rx,version_ack,sizeof version_ack);queued_rx_len=sizeof version_ack;
    ld2410_uart_poll_once();assert(s_uart_overflows==2 && s_uart_errors==1 && s_test_version_ack);
    /* Production ingress handles full chunks and leaves one latest measurement. */
    reset();s_test_state=UART_TEST_DONE;
    uint8_t bytes[256];memset(bytes,0x12,sizeof bytes);
    memcpy(bytes+sizeof bytes-sizeof measurement,measurement,sizeof measurement);
    ld2410_receive_bytes(bytes,sizeof bytes);
    assert(s_total_rx_bytes==256 && s_latest_pending && s_rx_stream_len==0);
    reset();ld2410_diag_begin();size_t before=hex_bytes;
    for(int i=0;i<40;i++)ld2410_diag_rx(bytes,sizeof bytes);
#if LD2410_UART_DIAGNOSTICS_ENABLED
    assert(hex_bytes-before==LD2410_DIAG_RX_BUDGET);
    clock_us+=LD2410_DIAG_WINDOW_US;before=hex_bytes;
    ld2410_diag_rx(bytes,sizeof bytes);assert(hex_bytes==before);
#else
    assert(hex_bytes==before);
#endif
}
static void bounded_timeouts(void) {
    reset();for(int i=0;i<100;i++){ld2410_selftest_step();clock_us+=800000;}
    assert(s_test_state==UART_TEST_DONE && writes==4 && s_timeouts==4 && !s_test_enter_ack);
    reset();ld2410_selftest_step();feed(enter_ack,sizeof enter_ack);
    for(int i=0;i<100;i++){ld2410_selftest_step();clock_us+=800000;}
    assert(s_test_state==UART_TEST_DONE && writes==5 && s_timeouts==4);
    reset();api_error=1;assert(ld2410_radar_init()!=ESP_OK);assert(s_initialized && !s_uart_initialized);
    reset();task_ok=0;assert(ld2410_radar_init()==ESP_ERR_NO_MEM);assert(s_initialized && !s_uart_initialized);
}
#endif
int main(void) {
#if LD2410_UART_SELFTEST_ENABLED
    encoder();RX_EXACT_TX_FRAME_MUST_NOT_BE_ACCEPTED_AS_ACK();runtime_rx_and_diagnostics();success_and_splits();malformed_and_nack();interleaved();adversarial_streams();bounded_timeouts();
#else
    reset();for(int i=0;i<100;i++)ld2410_selftest_step();assert(writes==0 && s_test_state==UART_TEST_DONE);
    feed(measurement,sizeof measurement);assert(s_latest_pending);
#endif
    puts("LD2410 production-driver host tests: PASS");return 0;
}
