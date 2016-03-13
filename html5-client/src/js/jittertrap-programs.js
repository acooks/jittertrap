/* jittertrap-programs.js */

/* global Mustache */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.programsModule = {};

  var programs = {};
  var nextPID = 123;

  var Program = function (json) {
    this.id = "program_" + nextPID++;
    this.name = json.name;
    console.log("name: "+json.name);
    programs[this.id] = this;
  };

  my.programsModule.addProgramHandler = function(event) {
    var pgm = $("#new_program").val();
    console.log("new program:" + pgm);
    loadProgram(pgm);
  };

  my.programsModule.templateProgram = JSON.stringify(
    {
      id: "program_123",
      name: "templateProgram",
      traps: [],
      impairments: {
        1: { delay: 10, jitter: 2, loss: 0,   trapid: 0},
        2: { delay: 15, jitter: 2, loss: 0.1, trapid: 0},
        3: { delay: 0,  jitter: 0, loss: 0,   trapid: 1},
      }
    },
    null,
    '\t'
  );

  var updateUI = function(pgm) {
    var programTable = $('#programs_table');

    $.get('/templates/program.html', function(template) {
      var template_data = { programName: pgm.name,
                            programUID:  pgm.id,
                          },
          rendered      = Mustache.render(template, template_data);

      console.log(rendered);

      programTable.find('tbody').append(rendered);

      // Remove button
      $("#"+pgm.id+"_delete").on('click', function(event) {
        // Remove from JS
        delete programs[pgm.id];

        // Remove from UI
        var tr = $(event.target).parents('tr');
        tr.remove();
      });

      // Reset button
      $("#"+pgm.id+"_reset").on('click', function(event) {
        pgm.reset();
      });
    });

    $('#add_program_modal button').get(1).click();
  };

  var loadProgram = function(pgm_txt) {
    var pgm = new Program(JSON.parse(pgm_txt));
    updateUI(pgm);
    console.log(programs);
  };

  return my;
}(JT));
