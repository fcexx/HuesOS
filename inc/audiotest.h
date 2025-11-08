#pragma once

/**
 * Audio Test Application
 * 
 * Test program for Intel HDA audio driver
 */

/* Main entry point */
void audiotest_main(void);

/* Individual test functions */
void audiotest_play_notes(void);
void audiotest_play_melody(void);
void audiotest_test_frequencies(void);
void audiotest_test_volume(void);
void audiotest_show_status(void);
void audiotest_menu(void);
