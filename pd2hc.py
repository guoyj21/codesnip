import copy
import pandas as pd

supported_chart_type = [
    'line',
    'column',
    'bar'
]


def serialize(df, config, output_type='js'):
    def serialize_chart(df, output, config):
        output['chart'] = {
            'alignTicks': False,
        }

        if 'core' not in config:
            raise RequiredFieldIsMissing('"core" is missing')

        if 'render_to' not in config['core']:
            raise RequiredFieldIsMissing('"render_to" is missing')
        else:
            output['chart']['renderTo'] = config['core']['render_to']

        if 'type' not in config['core']:
            output['chart']['type'] = 'line'
        else:
            chart_type = config['core']['type']
            if chart_type not in supported_chart_type:
                raise Exception('Chart type is not ready')
            output['chart']['type'] = chart_type

        for key in config.get('chart', {}).keys():
            output['chart'][key] = config['chart'][key]

    def serialize_colors(df, output, config):
        output['colors'] = [
            '#7cb5ec',
            '#90ed7d',
            '#f7a35c',
            '#f15c80',
            '#2b908f',
            '#f45b5b',
            '#91e8e1',
            '#8085e9'
        ]

        if 'colors' in config:
            output['colors'] = config['colors']

    def serialize_legend(df, output, config):
        output['legend'] = {
            'enabled': True
        }

        if 'legend' in config:
            for key in config['legend'].keys():
                output['legend'][key] = config['legend'][key]

    def serialize_series(df, output, config):
        def is_secondary(c, config):
            return c in config['core'].get('secondary_y', [])

        if 'sort_columns' in config:
            df = df.sort_index()

        columns = df.columns.values.tolist()
        series = df.to_dict('series')
        output['series'] = []

        for name in columns:
            data = series[name]
            if df[name].dtype.kind in 'biufc':
                sec = is_secondary(name, config)
                d = {
                    'name': name if not sec else name,
                    'yAxis': int(sec),
                    'data': list(zip(df.index, data.values.tolist()))
                }

                if int(sec):
                    d['type'] = config['core'].get('secondary_type', 'spline')

                output['series'].append(d)

    def serialize_title(df, output, config):
        output['title'] = {
            'text': 'title of chart'
        }

        if 'title' in config:
            for key in config['title'].keys():
                output['title'][key] = config['title'][key]

    def serialize_xAxis(df, output, config):
        output['xAxis'] = {}
        if df.index.name:
            output['xAxis']['title'] = {
                'text': df.index.name
            }

        if df.index.dtype.kind == 'M':
            output['xAxis']['type'] = 'datetime'

        if df.index.dtype.kind == 'O':
            output['xAxis']['categories'] = sorted(list(df.index))

        if 'xAxis' in config:
            for key in config['xAxis']:
                output['xAxis'][key] = config['xAxis'][key]

    def serialize_yAxis(df, output, config):
        yAxis = {
            'title': {
                'text': ''
            }
        }

        output['yAxis'] = [yAxis]

        if config['core'].get('secondary_y', []):
            yAxis2 = copy.deepcopy(yAxis)
            yAxis2['opposite'] = True
            yAxis2.setdefault('labels', {})
            yAxis2['labels']['format'] = '{value}%'
            yAxis2['max'] = 100
            yAxis2['gridLineWidth'] = 0
            output['yAxis'].append(yAxis2)

    output = {}
    df_copy = copy.deepcopy(df)

    serialize_chart(df_copy, output, config)
    serialize_colors(df_copy, output, config)
    serialize_legend(df_copy, output, config)
    serialize_series(df_copy, output, config)
    serialize_title(df_copy, output, config)
    serialize_xAxis(df_copy, output, config)
    serialize_yAxis(df_copy, output, config)

    if output_type == 'js':
        return 'new Highcharts.Chart(%s);' % pd.io.json.dumps(output)
    elif output_type == 'dict':
        return output
    elif output_type == 'json':
        return pd.io.json.dumps(output)


class RequiredFieldIsMissing(Exception):
    pass
