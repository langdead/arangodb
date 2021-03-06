/*global window, $, Backbone, document */

(function() {
  "use strict";

  $.get("cluster/amIDispatcher", function(data) {
    if (!data) {
      var url = window.location.origin;
      url += window.location.pathname;
      url = url.replace("cluster", "index");
      window.location.replace(url);
    }
  });
  window.location.hash = "";
  $(document).ready(function() {
    window.App = new window.ClusterRouter();

    Backbone.history.start();

    if(window.App.clusterPlan.get("plan")) {
      if(window.App.clusterPlan.isAlive()) {
        window.App.initial = window.App.showCluster;
      } else {
        window.App.initial = window.App.handleClusterDown;
      }
    } else {
      window.App.initial = window.App.planScenario;
    }
    window.App.initialRoute();
    window.App.handleResize();
  });

}());
