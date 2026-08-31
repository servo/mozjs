# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
from pathlib import Path

from mozperftest.layers import Layer
from mozperftest.metrics.exceptions import (
    EvalMetricsConfigurationError,
    EvalMetricsPayloadError,
    EvalMetricsResultError,
)
from mozperftest.utils import install_package, load_class_from_path


class EvalMetrics(Layer):
    name = "evalmetrics"
    activated = True
    arguments = {}
    evals_module = None

    def run(self, metadata):
        evaluations = (
            metadata.script.get("options", {}).get("default", {}).get("evaluations", {})
        )
        if not evaluations:
            raise EvalMetricsConfigurationError(
                "No evaluations were configured for this run."
            )

        eval_payloads = metadata.get_eval_payloads()
        if not eval_payloads:
            raise EvalMetricsPayloadError("No eval payloads were found.")

        per_metric_results = {}
        metric_to_config = {}

        for eval_name, eval_config in evaluations.items():
            eval_cls = load_class_from_path(
                eval_name,
                Path(self.mach_cmd.topsrcdir) / "toolkit/components/ml/eval/evals.py",
            )

            def __log(message):
                self.info("[eval] {message}", message=message)

            eval_instance = eval_cls(__log, eval_config)

            for requirement in eval_instance.requirements:
                install_package(
                    self.mach_cmd.virtualenv_manager,
                    requirement,
                )

            # Run the evals from toolkit/components/ml/eval.
            for test_name, payloads in eval_payloads:
                self.info(f"[eval] Running {eval_name} on {test_name}")
                results = eval_instance.run(payloads)
                if not isinstance(
                    results, list
                ):  # Allow evals to return a single result dict or a list of result dicts.
                    results = [results]
                for result in results:
                    metric_name = result.get("name")
                    if not metric_name:
                        raise EvalMetricsResultError(
                            "Eval metric result is missing a name"
                        )
                    lower_is_better = result.get("lowerIsBetter")
                    if lower_is_better is None:
                        raise EvalMetricsResultError(
                            f"Eval result for {metric_name} missing lowerIsBetter"
                        )
                    result["subtest"] = test_name
                    per_metric_results.setdefault(metric_name, []).append(result)
                    metric_to_config[metric_name] = eval_config

        output_dir = Path(self.get_arg("output")).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)

        for metric_name, results in per_metric_results.items():
            rows = []
            combined_values = []
            for result in results:
                test_name = result.get("subtest")
                values = result.get("values", [])
                if not test_name:
                    raise EvalMetricsResultError(
                        f"Eval metric result for {metric_name} missing subtest name"
                    )
                combined_values.extend(values)
                for value in values:
                    rows.append({test_name: value})

            file_name = f"eval-{metric_name.replace(os.sep, '_')}.json"
            eval_path = output_dir / file_name
            eval_path.write_text(json.dumps(rows, indent=2))

            summary_value = (
                sum(combined_values) / len(combined_values) if combined_values else None
            )

            config = metric_to_config.get(metric_name)
            if config is None:
                raise EvalMetricsConfigurationError(
                    f"Missing configuration for metric {metric_name}"
                )

            lower_is_better = results[0].get("lowerIsBetter")
            if lower_is_better is None:
                raise EvalMetricsResultError(
                    f"Missing lowerIsBetter for metric {metric_name}"
                )

            suite_result = {
                "name": metric_name,
                "framework": {"name": "mozperftest"},
                "results": str(eval_path),
                "unit": metric_name,
                "lowerIsBetter": lower_is_better,
                "shouldAlert": config.get("shouldAlert", False),
                "value": summary_value,
            }
            alert_threshold = config.get("alertThreshold")
            if alert_threshold is not None:
                suite_result["alertThreshold"] = alert_threshold
            metadata.add_result(suite_result)

        return metadata
