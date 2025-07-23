/* jittertrap-programs.js */

/* global Mustache */
/* global JT:true */

((my) => {
  'use strict';

  my.programsModule = {};

  const programs = {};
  let nextPID = 123;
  let runningProgram = null;

  const Program = function (json) {
    this.id = "program_" + nextPID++;
    this.name = json.name;
    this.timeoutHandles = {};
    this.impairments = json.impairments;

    programs[this.id] = this;

    this.play = function() {
      console.log("playing program: " + this.id);
      if (runningProgram && runningProgram.id !== this.id && runningProgram.stop) {
        runningProgram.stop();
      }
      $('.program-play-btn').removeClass('program-running');
      runningProgram = this;
      $("#"+this.id+"_play").addClass('program-running');
      $("#delay").prop('readonly', true);
      $("#jitter").prop('readonly', true);
      $("#loss").prop('readonly', true);
      $("#set_netem_button").prop('disabled', true);
      $("#clear_netem_button").prop('disabled', true);
      $("#netem_status").html("Program Running");

      for (let i in this.impairments) {
        if (json.impairments[i].stop) {
          console.log("stop at " + i);
          this.timeoutHandles[i] = setTimeout(() => {
              $("#delay").val(0);
              $("#jitter").val(0);
              $("#loss").val(0);
              JT.ws.set_netem();
              runningProgram.stop();
            },
            i * 1000
          );
        } else {
          this.timeoutHandles[i] = setTimeout((t, d, j, l) => {
              console.log(Date.now() + " t = " + t + ", d: " + d + ", j: " + j + ", l:" + l);
              $("#delay").val(d);
              $("#jitter").val(j);
              $("#loss").val(l);
              JT.ws.set_netem();
            },
            i*1000,
            i,
            runningProgram.impairments[i].delay,
            runningProgram.impairments[i].jitter,
            runningProgram.impairments[i].loss
          );
        }
      }
    };

    this.stop = function() {
      console.log("stopping program: " + this.id);
      if (this != runningProgram) {
        return runningProgram.stop();
      }
      for (let th in this.timeoutHandles) {
        clearTimeout(this.timeoutHandles[th]);
      }
      JT.ws.clear_netem();
      $("#"+this.id+"_play").removeClass('program-running');
      $("#delay").prop('readonly', false);
      $("#jitter").prop('readonly', false);
      $("#loss").prop('readonly', false);
      $("#set_netem_button").prop('disabled', false);
      $("#clear_netem_button").prop('disabled', false);
      $("#netem_status").html("Ready");
      runningProgram = null;
    };
  };

  my.programsModule.templateProgram = JSON.stringify(
    {
      name: "templateProgram",
      traps: [],
      impairments: {
        0: { delay: 0,  jitter: 0, loss: 0,   trapid: 0},
        5: { delay: 10, jitter: 2, loss: 0,   trapid: 0},
        10: { delay: 15, jitter: 2, loss: 0.1, trapid: 0},
        15: { delay: 0,  jitter: 0, loss: 0,   trapid: 1},
        20: { stop: true },
      }
    },
    null,
    '\t'
  );

  my.programsModule.addProgramHandler = function(event) {
    const pgm_txt = $("#new_program").val();
    console.log("new program:" + pgm_txt);

    const pgm = new Program(JSON.parse(pgm_txt));
    updateUI(pgm);
  };

  const updateUI = function(pgm) {
    const programTable = $('#programs_table');

    $.get('/templates/program.html', (template) => {
      const template_data = { programName: pgm.name,
                              programUID: pgm.id,
                            },
          rendered      = Mustache.render(template, template_data);

      programTable.find('tbody').append(rendered);

      // Play button
      $("#"+pgm.id+"_play").on('click', () => {
        pgm.play();
      });

      // Stop button
      $("#"+pgm.id+"_stop").on('click', () => {
        pgm.stop();
      });

      // Edit button
      $("#"+pgm.id+"_edit").on('click', () => {
        $("#new_program").val(JSON.stringify(pgm, null, '\t'));
      });

      // Remove button
      $("#"+pgm.id+"_delete").on('click', (event) => {
        // Remove from JS
        delete programs[pgm.id];

        // Remove from UI
        const tr = $(event.target).closest('tr');
        tr.remove();
      });

    });

    // Close the modal dialog
    $('#add_program_modal').modal('hide');
  };

  my.programsModule.processNetemMsg = function (params) {
    if (params.delay === -1 && params.jitter === -1 && params.loss === -1) {
      $("#netem_status").html("No active impairment on device. Set parameters to activate.");
      $("#delay").val("0");
      $("#jitter").val("0");
      $("#loss").val("0");
    } else {
      $("#delay").val(params.delay);
      $("#jitter").val(params.jitter);
      /* params.loss is a 10th of a percent (integer). convert it to float */
      $("#loss").val(0.1 * params.loss);
    }
    if (runningProgram) {
      $("#netem_status").html("Program Running");
    } else {
      // response to ad-hoc set
      $("#netem_status").html("Ready");
    }
  };

})(JT);
