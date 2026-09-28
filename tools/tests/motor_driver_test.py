"""Compile the actual motor driver with RMT fakes; no motor is operated."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include <algorithm>
#include <atomic>
#include <iterator>
#include <functional>
#include <cstdint>
#include <cassert>
#include <vector>
#define LOG_BLE(...) ((void)0)
#define DEBUG_ENABLE_LOADCELL_MOCK 0
#define HW_GRINDER_SETTLING_TIME_MS 100
#define HW_MOTOR_DEADMAN_TIMEOUT_MS 1000
#define HW_MOTOR_DEADMAN_CHECK_INTERVAL_MS 50
#define IRAM_ATTR
using gpio_num_t=int;
using esp_err_t=int;
using rmt_channel_handle_t=void*;
using rmt_encoder_handle_t=void*;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, RMT_CLK_SRC_DEFAULT=0, GPIO_MODE_OUTPUT=0;
struct rmt_symbol_word_t { uint32_t duration0:15; uint32_t level0:1; uint32_t duration1:15; uint32_t level1:1; };
struct rmt_tx_channel_config_t { int gpio_num,clk_src,resolution_hz,mem_block_symbols,trans_queue_depth; };
struct rmt_copy_encoder_config_t {};
struct rmt_transmit_config_t { int loop_count=0; };
struct rmt_tx_done_event_data_t { size_t num_symbols; };
using rmt_tx_done_callback_t=bool(*)(rmt_channel_handle_t,const rmt_tx_done_event_data_t*,void*);
struct rmt_tx_event_callbacks_t { rmt_tx_done_callback_t on_trans_done; };
using esp_timer_cb_t=void(*)(void*);
using esp_timer_handle_t=void*;
enum esp_timer_dispatch_t { ESP_TIMER_TASK };
struct esp_timer_create_args_t {
 esp_timer_cb_t callback;void* arg;esp_timer_dispatch_t dispatch_method;const char* name;bool skip_unhandled_events;
};
int tx_error=0, enable_error=0, disable_error=0, register_error=0, timer_start_error=0;
bool active=false;
const rmt_symbol_word_t* payload=nullptr;
size_t count=0;
int loops=0;
int gpio_low_calls=0;
rmt_tx_done_callback_t done_callback=nullptr;
void* done_context=nullptr;
esp_timer_cb_t timer_callback=nullptr;
void* timer_context=nullptr;
uint64_t timer_period_us=0;
int timers_deleted=0;
unsigned long now_ms=100;
unsigned long millis(){return now_ms;}
int gpio_set_direction(int,int){return 0;}
int gpio_pullup_dis(int){return 0;}
int gpio_pulldown_en(int){return 0;}
int gpio_set_level(int,int value){assert(value==0);++gpio_low_calls;return 0;}
int rmt_new_tx_channel(const rmt_tx_channel_config_t*,void** c){*c=(void*)1;return 0;}
int rmt_new_copy_encoder(const rmt_copy_encoder_config_t*,void** e){*e=(void*)2;return 0;}
int rmt_tx_register_event_callbacks(void*,const rmt_tx_event_callbacks_t* callbacks,void* context){
    if(register_error)return register_error;
    done_callback=callbacks->on_trans_done;done_context=context;return 0;
}
int rmt_enable(void*){return enable_error;}
int rmt_disable(void*){if(disable_error)return disable_error; active=false;return 0;}
int rmt_encoder_reset(void*){assert(!active);return 0;}
int rmt_del_channel(void*){return 0;}
int rmt_del_encoder(void*){assert(!active);return 0;}
int rmt_transmit(void*,void*,const void* data,size_t bytes,const rmt_transmit_config_t* config){
    assert(!active);
    payload=static_cast<const rmt_symbol_word_t*>(data);count=bytes/sizeof(*payload);loops=config->loop_count;
    if(tx_error)return tx_error;
    active=true;return 0;
}
int esp_timer_create(const esp_timer_create_args_t* args,esp_timer_handle_t* timer){
    assert(args->dispatch_method==ESP_TIMER_TASK);
    timer_callback=args->callback;timer_context=args->arg;*timer=(void*)3;return 0;
}
int esp_timer_start_periodic(esp_timer_handle_t,uint64_t period){
    if(timer_start_error)return timer_start_error;
    timer_period_us=period;return 0;
}
int esp_timer_delete(esp_timer_handle_t){++timers_deleted;timer_callback=nullptr;return 0;}
// The driver reports a finished finite transmission from its interrupt.
void finish_transmission(){
    assert(active && loops==0);active=false;
    rmt_tx_done_event_data_t event{count};done_callback(nullptr,&event,done_context);
}
void run_deadman(){timer_callback(timer_context);}
enum class UIGrindEvent { BACKGROUND_CHANGE };
enum class GrindPhase { IDLE };
struct GrindEventData {
 UIGrindEvent event;GrindPhase phase;float current_weight;int progress_percent;
 const char* phase_display_text;bool show_taring_text;bool background_active;
};
'''
CASES = r'''
int main(){
 Grinder motor;motor.init(16);assert(motor.is_initialized());
 assert(gpio_low_calls==1); // pin held LOW before RMT takes it over
 assert(done_callback && timer_callback && timer_period_us==HW_MOTOR_DEADMAN_CHECK_INTERVAL_MS*1000ULL);
 for(uint32_t ms=1;ms<=2064;++ms){
  motor.start_pulse_rmt(ms);assert(motor.is_grinding());assert(loops==0);
  assert(!motor.is_pulse_complete()); // queued, not started: must remain active
  uint32_t high=0,low=0;bool ended=false;
  for(size_t i=0;i<count;++i){
   uint32_t durations[]={payload[i].duration0,payload[i].duration1};
   uint32_t levels[]={payload[i].level0,payload[i].level1};
   for(int half=0;half<2;++half){
    if(durations[half]==0){assert(ended);continue;}
    if(levels[half]){assert(!ended);high+=durations[half];}
    else{ended=true;low+=durations[half];}
   }
  }
  assert(high==ms*1000 && low==1);
  finish_transmission();assert(motor.is_pulse_complete());assert(!motor.is_grinding());
 }
 motor.start();assert(motor.is_grinding() && loops==-1);
 assert(payload[0].level0==1 && payload[0].level1==1);
 motor.start_pulse_rmt(100); // cancels old transmission before modifying payload
 assert(motor.is_grinding() && loops==0);
 motor.start();motor.stop();assert(!motor.is_grinding());
 for(uint32_t invalid: {0U,2065U,UINT32_MAX}){
  motor.start_pulse_rmt(invalid);assert(!motor.is_grinding());
 }
 tx_error=-1;motor.start();assert(!motor.is_grinding());
 motor.start_pulse_rmt(100);assert(!motor.is_grinding());tx_error=0;
 now_ms+=5000;run_deadman();assert(!motor.has_safety_stop()); // failed start is not armed

 // A pulse whose completion is never reported is stopped after its length + 500 ms.
 motor.start_pulse_rmt(100);now_ms+=599;assert(!motor.is_pulse_complete());
 now_ms+=1;assert(motor.is_pulse_complete());assert(!motor.is_grinding() && !active);

 // Finite pulses end in hardware and are not subject to the dead-man.
 motor.start_pulse_rmt(2000);now_ms+=1500;run_deadman();assert(!motor.has_safety_stop());
 finish_transmission();assert(motor.is_pulse_complete());

 // Regular keep-alives hold a continuous run; a stop disarms the dead-man.
 motor.start();
 for(int step=1;step<=80;++step){
  now_ms+=50;if(step%19==0)motor.keep_alive();
  run_deadman();assert(!motor.has_safety_stop());
 }
 motor.stop();now_ms+=5000;run_deadman();assert(!motor.has_safety_stop());

 // A continuous run without a keep-alive for the timeout is forced LOW and latched.
 motor.start();motor.keep_alive();int previous=gpio_low_calls;
 now_ms+=HW_MOTOR_DEADMAN_TIMEOUT_MS-1;run_deadman();assert(!motor.has_safety_stop());
 now_ms+=1;run_deadman();assert(motor.has_safety_stop() && gpio_low_calls==previous+1);
 run_deadman();assert(gpio_low_calls==previous+1);
 motor.stop();assert(!motor.is_grinding() && motor.has_safety_stop());
 motor.start();assert(!motor.is_grinding());
 motor.start_pulse_rmt(100);assert(!motor.is_grinding());

 Grinder faulty;faulty.init(16);assert(faulty.is_initialized());
 faulty.start();disable_error=-1;previous=gpio_low_calls;faulty.stop();
 assert(!faulty.is_initialized() && !faulty.is_grinding() && gpio_low_calls>previous);
 faulty.start();assert(!faulty.is_grinding());
 now_ms+=5000;run_deadman();assert(!faulty.has_safety_stop());
 disable_error=0;active=false;

 enable_error=-1;
 Grinder failed;failed.init(16);assert(!failed.is_initialized());
 enable_error=0;register_error=-1;
 Grinder no_callback;no_callback.init(16);assert(!no_callback.is_initialized());
 register_error=0;timer_start_error=-1;int deleted=timers_deleted;previous=gpio_low_calls;
 Grinder no_deadman;no_deadman.init(16);
 assert(!no_deadman.is_initialized() && timers_deleted==deleted+1 && gpio_low_calls>previous+1);
 no_deadman.start();no_deadman.start_pulse_rmt(100);assert(!no_deadman.is_grinding() && !active);
}
'''


def source_without_includes(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class MotorDriverTest(unittest.TestCase):
    def test_actual_driver(self):
        code = (STUBS + source_without_includes(ROOT / "src/hardware/grinder.h")
                + "\n" + source_without_includes(ROOT / "src/hardware/grinder.cpp")
                + "\n" + CASES)
        with tempfile.TemporaryDirectory(prefix="smart-grind-motor-test-") as folder:
            source = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            source.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
