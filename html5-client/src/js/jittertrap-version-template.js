/* jittertrap-version-template.js */

/* global JT: true */

JT = (function (my) {
  'use strict';
  my.version = {};
  my.version.maintainerVersion = '##MAINTAINER-VERSION##';
  my.version.repo = '##GIT-REPO##';
  my.version.branch = '##GIT-BRANCH##';
  my.version.commit = '##GIT-COMMIT##';
  my.version.commitTime = '##GIT-COMMIT-TS##';
  my.version.isClean = '##GIT-CLEAN##';

 return my;
}(JT));
/* End of jittertrap-version-template.js */

