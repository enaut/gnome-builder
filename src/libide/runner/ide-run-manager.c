/* ide-run-manager.c
 *
 * Copyright © 2016 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "ide-run-manager"

#include <glib/gi18n.h>

#include "ide-context.h"
#include "ide-debug.h"

#include "buildsystem/ide-build-manager.h"
#include "buildsystem/ide-build-system.h"
#include "buildsystem/ide-build-target.h"
#include "buildsystem/ide-configuration.h"
#include "buildsystem/ide-configuration-manager.h"
#include "buildsystem/ide-environment.h"
#include "runner/ide-run-manager.h"
#include "runner/ide-run-manager-private.h"
#include "runner/ide-runner.h"
#include "runtimes/ide-runtime.h"

struct _IdeRunManager
{
  IdeObject                parent_instance;

  GCancellable            *cancellable;
  IdeBuildTarget          *build_target;

  const IdeRunHandlerInfo *handler;
  GList                   *handlers;

  guint                    busy : 1;
};

static void initable_iface_init                      (GInitableIface *iface);
static void ide_run_manager_actions_run              (IdeRunManager  *self,
                                                      GVariant       *param);
static void ide_run_manager_actions_run_with_handler (IdeRunManager  *self,
                                                      GVariant       *param);
static void ide_run_manager_actions_stop             (IdeRunManager  *self,
                                                      GVariant       *param);

DZL_DEFINE_ACTION_GROUP (IdeRunManager, ide_run_manager, {
  { "run", ide_run_manager_actions_run },
  { "run-with-handler", ide_run_manager_actions_run_with_handler, "s" },
  { "stop", ide_run_manager_actions_stop },
})

G_DEFINE_TYPE_EXTENDED (IdeRunManager, ide_run_manager, IDE_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                        G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP,
                                               ide_run_manager_init_action_group))

enum {
  PROP_0,
  PROP_BUSY,
  PROP_HANDLER,
  PROP_BUILD_TARGET,
  N_PROPS
};

enum {
  RUN,
  STOPPED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static void
ide_run_manager_real_run (IdeRunManager *self,
                          IdeRunner     *runner)
{
  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (IDE_IS_RUNNER (runner));

  /*
   * If the current handler has a callback specified (our default "run" handler
   * does not), then we need to allow that handler to prepare the runner.
   */
  if (self->handler != NULL && self->handler->handler != NULL)
    self->handler->handler (self, runner, self->handler->handler_data);
}

static void
ide_run_handler_info_free (gpointer data)
{
  IdeRunHandlerInfo *info = data;

  g_free (info->id);
  g_free (info->title);
  g_free (info->icon_name);
  g_free (info->accel);

  if (info->handler_data_destroy)
    info->handler_data_destroy (info->handler_data);

  g_slice_free (IdeRunHandlerInfo, info);
}

static void
ide_run_manager_finalize (GObject *object)
{
  IdeRunManager *self = (IdeRunManager *)object;

  g_clear_object (&self->cancellable);
  g_clear_object (&self->build_target);

  g_list_free_full (self->handlers, ide_run_handler_info_free);
  self->handlers = NULL;

  G_OBJECT_CLASS (ide_run_manager_parent_class)->finalize (object);
}

static void
ide_run_manager_update_action_enabled (IdeRunManager *self)
{
  IdeBuildManager *build_manager;
  IdeContext *context;
  gboolean can_build;

  g_assert (IDE_IS_RUN_MANAGER (self));

  context = ide_object_get_context (IDE_OBJECT (self));
  build_manager = ide_context_get_build_manager (context);
  can_build = ide_build_manager_get_can_build (build_manager);

  ide_run_manager_set_action_enabled (self, "run",
                                      self->busy == FALSE && can_build == TRUE);
  ide_run_manager_set_action_enabled (self, "run-with-handler",
                                      self->busy == FALSE && can_build == TRUE);
  ide_run_manager_set_action_enabled (self, "stop", self->busy == TRUE);
}

static void
ide_run_manager_notify_can_build (IdeRunManager   *self,
                                  GParamSpec      *pspec,
                                  IdeBuildManager *build_manager)
{
  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (G_IS_PARAM_SPEC (pspec));
  g_assert (IDE_IS_BUILD_MANAGER (build_manager));

  ide_run_manager_update_action_enabled (self);

  IDE_EXIT;
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  IdeRunManager *self = (IdeRunManager *)initable;
  IdeBuildManager *build_manager;
  IdeContext *context;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  context = ide_object_get_context (IDE_OBJECT (self));
  build_manager = ide_context_get_build_manager (context);

  g_signal_connect_object (build_manager,
                           "notify::can-build",
                           G_CALLBACK (ide_run_manager_notify_can_build),
                           self,
                           G_CONNECT_SWAPPED);

  ide_run_manager_update_action_enabled (self);

  IDE_RETURN (TRUE);
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = initable_init;
}

static void
ide_run_manager_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  IdeRunManager *self = IDE_RUN_MANAGER (object);

  switch (prop_id)
    {
    case PROP_BUSY:
      g_value_set_boolean (value, ide_run_manager_get_busy (self));
      break;

    case PROP_HANDLER:
      g_value_set_string (value, ide_run_manager_get_handler (self));
      break;

    case PROP_BUILD_TARGET:
      g_value_set_object (value, ide_run_manager_get_build_target (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_run_manager_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  IdeRunManager *self = IDE_RUN_MANAGER (object);

  switch (prop_id)
    {
    case PROP_BUILD_TARGET:
      ide_run_manager_set_build_target (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
ide_run_manager_class_init (IdeRunManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_run_manager_finalize;
  object_class->get_property = ide_run_manager_get_property;
  object_class->set_property = ide_run_manager_set_property;

  properties [PROP_BUSY] =
    g_param_spec_boolean ("busy",
                          "Busy",
                          "Busy",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_HANDLER] =
    g_param_spec_string ("handler",
                         "Handler",
                         "Handler",
                         "run",
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUILD_TARGET] =
    g_param_spec_object ("build-target",
                         "Build Target",
                         "The IdeBuildTarget that will be run",
                         IDE_TYPE_BUILD_TARGET,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * IdeRunManager::run:
   * @self: An #IdeRunManager
   * @runner: An #IdeRunner
   *
   * This signal is emitted right before ide_runner_run_async() is called
   * on an #IdeRunner. It can be used by plugins to tweak things right
   * before the runner is executed.
   *
   * The current run handler (debugger, profiler, etc) is run as the default
   * handler for this function. So connect with %G_SIGNAL_AFTER if you want
   * to be nofied after the run handler has executed. It's unwise to change
   * things that the run handler might expect. Generally if you want to
   * change settings, do that before the run handler has exected.
   */
  signals [RUN] =
    g_signal_new_class_handler ("run",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (ide_run_manager_real_run),
                                NULL,
                                NULL,
                                NULL,
                                G_TYPE_NONE,
                                1,
                                IDE_TYPE_RUNNER);

  /**
   * IdeRunManager::stopped:
   *
   * This signal is emitted when the run manager has stopped the currently
   * executing inferior.
   */
  signals [STOPPED] =
    g_signal_new ("stopped",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
}

gboolean
ide_run_manager_get_busy (IdeRunManager *self)
{
  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), FALSE);

  return self->busy;
}

static gboolean
ide_run_manager_check_busy (IdeRunManager  *self,
                            GError        **error)
{
  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (error != NULL);

  if (ide_run_manager_get_busy (self))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_BUSY,
                   "%s",
                   _("Cannot run target, another target is running"));
      return TRUE;
    }

  return FALSE;
}

static void
ide_run_manager_run_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  IdeRunner *runner = (IdeRunner *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  IdeRunManager *self;

  IDE_ENTRY;

  g_assert (IDE_IS_RUNNER (runner));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!ide_runner_run_finish (runner, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);

  g_signal_emit (self, signals [STOPPED], 0);

  IDE_EXIT;
}

static void
do_run_async (IdeRunManager *self,
              GTask         *task)
{
  g_auto(GStrv) run_argv = NULL;
  IdeBuildTarget *build_target;
  IdeContext *context;
  IdeConfigurationManager *config_manager;
  IdeConfiguration *config;
  IdeEnvironment *environment;
  IdeRuntime *runtime;
  g_autoptr(IdeRunner) runner = NULL;
  GCancellable *cancellable;
  const gchar *run_opts;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (G_IS_TASK (task));

  build_target = g_task_get_task_data (task);
  context = ide_object_get_context (IDE_OBJECT (self));

  g_assert (IDE_IS_BUILD_TARGET (build_target));
  g_assert (IDE_IS_CONTEXT (context));

  config_manager = ide_context_get_configuration_manager (context);
  config = ide_configuration_manager_get_current (config_manager);
  runtime = ide_configuration_get_runtime (config);

  if (runtime == NULL)
    {
      g_task_return_new_error (task,
                               IDE_RUNTIME_ERROR,
                               IDE_RUNTIME_ERROR_NO_SUCH_RUNTIME,
                               "%s “%s”",
                               _("Failed to locate runtime"),
                               ide_configuration_get_runtime_id (config));
      IDE_EXIT;
    }

  runner = ide_runtime_create_runner (runtime, build_target);
  cancellable = g_task_get_cancellable (task);

  g_assert (IDE_IS_RUNNER (runner));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /* Add our run arguments if specified in the config. */
  if (NULL != (run_opts = ide_configuration_get_run_opts (config)))
    {
      g_auto(GStrv) argv = NULL;
      gint argc;

      if (g_shell_parse_argv (run_opts, &argc, &argv, NULL))
        {
          for (gint i = 0; i < argc; i++)
            ide_runner_append_argv (runner, argv[i]);
        }
    }

  /* Add our environment variables. Currently, these are coming
   * from the *build* environment because we do not yet have a
   * way to differentiate between build environment and runtime
   * for the application.
   */
  environment = ide_runner_get_environment (runner);
  ide_environment_setenv (environment, "G_MESSAGES_DEBUG", "all");
  ide_environment_copy_into (ide_configuration_get_environment (config), environment, TRUE);

  g_signal_emit (self, signals [RUN], 0, runner);

  if (ide_runner_get_failed (runner))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to execute the application");
      IDE_EXIT;
    }

  ide_runner_run_async (runner,
                        cancellable,
                        ide_run_manager_run_cb,
                        g_object_ref (task));

  IDE_EXIT;
}

static void
ide_run_manager_run_discover_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  IdeRunManager *self = (IdeRunManager *)object;
  g_autoptr(IdeBuildTarget) build_target = NULL;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  build_target = ide_run_manager_discover_default_target_finish (self, result, &error);

  if (build_target == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      IDE_EXIT;
    }

  ide_run_manager_set_build_target (self, build_target);

  g_task_set_task_data (task, g_steal_pointer (&build_target), g_object_unref);

  do_run_async (self, task);

  IDE_EXIT;
}

static void
ide_run_manager_install_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  IdeBuildManager *build_manager = (IdeBuildManager *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  IdeRunManager *self;
  IdeBuildTarget *build_target;
  GCancellable *cancellable;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILD_MANAGER (build_manager));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (IDE_IS_RUN_MANAGER (self));

  if (!ide_build_manager_execute_finish (build_manager, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      IDE_EXIT;
    }

  build_target = ide_run_manager_get_build_target (self);

  if (build_target == NULL)
    {
      cancellable = g_task_get_cancellable (task);
      g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

      ide_run_manager_discover_default_target_async (self,
                                                     cancellable,
                                                     ide_run_manager_run_discover_cb,
                                                     g_steal_pointer (&task));
      IDE_EXIT;
    }

  g_task_set_task_data (task, g_object_ref (build_target), g_object_unref);

  do_run_async (self, task);

  IDE_EXIT;
}

static void
ide_run_manager_task_completed (IdeRunManager *self,
                                GParamSpec    *pspec,
                                GTask         *task)
{
  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (pspec != NULL);
  g_assert (G_IS_TASK (task));

  self->busy = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY]);

  ide_run_manager_update_action_enabled (self);

  IDE_EXIT;
}

static void
ide_run_manager_do_install_before_run (IdeRunManager *self,
                                       GTask         *task)
{
  IdeBuildManager *build_manager;
  IdeContext *context;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (G_IS_TASK (task));

  context = ide_object_get_context (IDE_OBJECT (self));
  build_manager = ide_context_get_build_manager (context);

  /*
   * First we need to make sure the target is up to date and installed
   * so that all the dependent resources are available.
   */

  self->busy = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY]);

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (ide_run_manager_task_completed),
                           self,
                           G_CONNECT_SWAPPED);

  ide_build_manager_execute_async (build_manager,
                                   IDE_BUILD_PHASE_INSTALL,
                                   g_task_get_cancellable (task),
                                   ide_run_manager_install_cb,
                                   g_object_ref (task));

  ide_run_manager_update_action_enabled (self);

  IDE_EXIT;
}

void
ide_run_manager_run_async (IdeRunManager       *self,
                           IdeBuildTarget      *build_target,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GCancellable) local_cancellable = NULL;
  g_autoptr(GError) error = NULL;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_RUN_MANAGER (self));
  g_return_if_fail (!build_target || IDE_IS_BUILD_TARGET (build_target));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (cancellable == NULL)
    cancellable = local_cancellable = g_cancellable_new ();

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_run_manager_run_async);

  g_set_object (&self->cancellable, cancellable);

  if (ide_run_manager_check_busy (self, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      IDE_EXIT;
    }

  if (build_target != NULL)
    ide_run_manager_set_build_target (self, build_target);

  ide_run_manager_do_install_before_run (self, task);

  IDE_EXIT;
}

gboolean
ide_run_manager_run_finish (IdeRunManager  *self,
                            GAsyncResult   *result,
                            GError        **error)
{
  gboolean ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  ret = g_task_propagate_boolean (G_TASK (result), error);

  IDE_RETURN (ret);
}

static gboolean
do_cancel_in_timeout (gpointer user_data)
{
  g_autoptr(GCancellable) cancellable = user_data;

  IDE_ENTRY;

  g_assert (G_IS_CANCELLABLE (cancellable));

  if (!g_cancellable_is_cancelled (cancellable))
    g_cancellable_cancel (cancellable);

  IDE_RETURN (G_SOURCE_REMOVE);
}

void
ide_run_manager_cancel (IdeRunManager *self)
{
  IDE_ENTRY;

  g_return_if_fail (IDE_IS_RUN_MANAGER (self));

  if (self->cancellable != NULL)
    g_timeout_add (0, do_cancel_in_timeout, g_object_ref (self->cancellable));

  IDE_EXIT;
}

void
ide_run_manager_set_handler (IdeRunManager *self,
                             const gchar   *id)
{
  g_return_if_fail (IDE_IS_RUN_MANAGER (self));

  self->handler = NULL;

  for (GList *iter = self->handlers; iter; iter = iter->next)
    {
      const IdeRunHandlerInfo *info = iter->data;

      if (g_strcmp0 (info->id, id) == 0)
        {
          self->handler = info;
          IDE_TRACE_MSG ("run handler set to %s", info->title);
          g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HANDLER]);
          break;
        }
    }
}

void
ide_run_manager_add_handler (IdeRunManager  *self,
                             const gchar    *id,
                             const gchar    *title,
                             const gchar    *icon_name,
                             const gchar    *accel,
                             IdeRunHandler   run_handler,
                             gpointer        user_data,
                             GDestroyNotify  user_data_destroy)
{
  IdeRunHandlerInfo *info;
  DzlShortcutManager *manager;
  DzlShortcutTheme *theme;
  g_autofree gchar *action_name = NULL;
  GApplication *app;

  g_return_if_fail (IDE_IS_RUN_MANAGER (self));
  g_return_if_fail (id != NULL);
  g_return_if_fail (title != NULL);

  info = g_slice_new (IdeRunHandlerInfo);
  info->id = g_strdup (id);
  info->title = g_strdup (title);
  info->icon_name = g_strdup (icon_name);
  info->accel = g_strdup (accel);
  info->handler = run_handler;
  info->handler_data = user_data;
  info->handler_data_destroy = user_data_destroy;

  self->handlers = g_list_append (self->handlers, info);

  app = g_application_get_default ();
  manager = dzl_application_get_shortcut_manager (DZL_APPLICATION (app));
  theme = g_object_ref (dzl_shortcut_manager_get_theme (manager));

  action_name = g_strdup_printf ("run-manager.run-with-handler('%s')", id);

  dzl_shortcut_manager_add_action (manager,
                                   action_name,
                                   NC_("shortcut window", "Workbench shortcuts"),
                                   NC_("shortcut window", "Build and Run"),
                                   NC_("shortcut window", title),
                                   NULL);

  dzl_shortcut_theme_set_accel_for_action (theme, action_name, accel, DZL_SHORTCUT_PHASE_DISPATCH);

  if (self->handler == NULL)
    self->handler = info;
}

void
ide_run_manager_remove_handler (IdeRunManager *self,
                                const gchar   *id)
{
  g_return_if_fail (IDE_IS_RUN_MANAGER (self));
  g_return_if_fail (id != NULL);

  for (GList *iter = self->handlers; iter; iter = iter->next)
    {
      IdeRunHandlerInfo *info = iter->data;

      if (g_strcmp0 (info->id, id) == 0)
        {
          self->handlers = g_list_remove_link (self->handlers, iter);

          if (self->handler == info && self->handlers != NULL)
            self->handler = self->handlers->data;
          else
            self->handler = NULL;

          ide_run_handler_info_free (info);

          break;
        }
    }
}

/**
 * ide_run_manager_get_build_target:
 *
 * Gets the build target that will be executed by the run manager which
 * was either specified to ide_run_manager_run_async() or determined by
 * the build system.
 *
 * Returns: (transfer none): An #IdeBuildTarget or %NULL if no build target
 *   has been set.
 */
IdeBuildTarget *
ide_run_manager_get_build_target (IdeRunManager *self)
{
  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), NULL);

  return self->build_target;
}

void
ide_run_manager_set_build_target (IdeRunManager  *self,
                                  IdeBuildTarget *build_target)
{
  g_return_if_fail (IDE_IS_RUN_MANAGER (self));
  g_return_if_fail (IDE_IS_BUILD_TARGET (build_target));

  if (g_set_object (&self->build_target, build_target))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUILD_TARGET]);
}

static IdeBuildTarget *
find_best_target (GPtrArray *targets)
{
  IdeBuildTarget *ret = NULL;
  guint i;

  g_assert (targets != NULL);

  /* TODO:
   *
   * This is just a barebones way to try to discover a target that matters. We
   * could probably defer this off to the build system. Either way, it's shit
   * and should be thought through by someone.
   */

  for (i = 0; i < targets->len; i++)
    {
      IdeBuildTarget *target = g_ptr_array_index (targets, i);
      g_autoptr(GFile) installdir = NULL;

      installdir = ide_build_target_get_install_directory (target);

      if (installdir == NULL)
        continue;

      if (ret == NULL)
        ret = target;
    }

  return ret;
}

static void
ide_run_manager_discover_default_target_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  IdeBuildSystem *build_system = (IdeBuildSystem *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GPtrArray) targets = NULL;
  g_autoptr(GError) error = NULL;
  IdeBuildTarget *best_match;

  IDE_ENTRY;

  g_assert (IDE_IS_BUILD_SYSTEM (build_system));
  g_assert (G_IS_ASYNC_RESULT (result));

  targets = ide_build_system_get_build_targets_finish (build_system, result, &error);

  if (targets == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      IDE_EXIT;
    }

  best_match = find_best_target (targets);

  if (best_match == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to locate build target");
      IDE_EXIT;
    }

  g_task_return_pointer (task, g_object_ref (best_match), g_object_unref);

  IDE_EXIT;
}

void
ide_run_manager_discover_default_target_async (IdeRunManager       *self,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  IdeBuildSystem *build_system;
  IdeContext *context;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_RUN_MANAGER (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_run_manager_discover_default_target_async);

  context = ide_object_get_context (IDE_OBJECT (self));
  build_system = ide_context_get_build_system (context);

  ide_build_system_get_build_targets_async (build_system,
                                            cancellable,
                                            ide_run_manager_discover_default_target_cb,
                                            g_object_ref (task));

  IDE_EXIT;
}

/**
 * ide_run_manager_discover_default_target_finish:
 *
 * Returns: (transfer full): An #IdeBuildTarget if successful; otherwise %NULL
 *   and @error is set.
 */
IdeBuildTarget *
ide_run_manager_discover_default_target_finish (IdeRunManager  *self,
                                                GAsyncResult   *result,
                                                GError        **error)
{
  IdeBuildTarget *ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  ret = g_task_propagate_pointer (G_TASK (result), error);

  IDE_RETURN (ret);
}

const GList *
_ide_run_manager_get_handlers (IdeRunManager *self)
{
  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), NULL);

  return self->handlers;
}

const gchar *
ide_run_manager_get_handler (IdeRunManager *self)
{
  g_return_val_if_fail (IDE_IS_RUN_MANAGER (self), NULL);

  if (self->handler != NULL)
    return self->handler->id;

  return NULL;
}

static void
ide_run_manager_run_action_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  IdeRunManager *self = (IdeRunManager *)object;
  IdeContext *context;
  g_autoptr(GError) error = NULL;

  g_assert (IDE_IS_RUN_MANAGER (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  context = ide_object_get_context (IDE_OBJECT (self));

  /* Propagate the error to the context */
  if (!ide_run_manager_run_finish (self, result, &error))
    ide_context_warning (context, "%s", error->message);
}

static void
ide_run_manager_actions_run (IdeRunManager *self,
                             GVariant      *param)
{
  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));

  ide_run_manager_run_async (self,
                             NULL,
                             NULL,
                             ide_run_manager_run_action_cb,
                             NULL);

  IDE_EXIT;
}

static void
ide_run_manager_actions_run_with_handler (IdeRunManager *self,
                                          GVariant      *param)
{
  const gchar *handler = NULL;
  g_autoptr(GVariant) sunk = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));

  if (param != NULL)
  {
    handler = g_variant_get_string (param, NULL);
    if (g_variant_is_floating (param))
      sunk = g_variant_ref_sink (param);
  }

  /* Use specified handler, if provided */
  if (!dzl_str_empty0 (handler))
    ide_run_manager_set_handler (self, handler);

  ide_run_manager_run_async (self,
                             NULL,
                             NULL,
                             ide_run_manager_run_action_cb,
                             NULL);

  IDE_EXIT;
}

static void
ide_run_manager_actions_stop (IdeRunManager *self,
                              GVariant      *param)
{
  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (self));

  ide_run_manager_cancel (self);

  IDE_EXIT;
}

static void
ide_run_manager_init (IdeRunManager *self)
{
  ide_run_manager_add_handler (self,
                               "run",
                               _("Run"),
                               "media-playback-start-symbolic",
                               "<primary>F5",
                               NULL,
                               NULL,
                               NULL);
}
